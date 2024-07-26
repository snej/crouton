//
// Backtrace.hh
//
// Copyright © 2018-Present Couchbase, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include <functional>
#include <memory>
#include <iosfwd>
#include <string>
#include <typeinfo>
#include <vector>

namespace cpptrace {
    struct stacktrace;
}
namespace crouton {
    namespace mini {
        class ostream;
    }

    /** Captures a backtrace of the current thread, and can convert it to human-readable form. */
    class Backtrace {
    public:
        /// Captures a backtrace and returns a shared pointer to the instance.
        static std::shared_ptr<Backtrace> capture(unsigned skipFrames =0, unsigned maxFrames =50);

        /// Captures a backtrace, unless maxFrames is zero.
        /// @param skipFrames  Number of frames to skip at top of stack
        /// @param maxFrames  Maximum number of frames to capture
        explicit Backtrace(unsigned skipFrames =0, unsigned maxFrames =50);
        ~Backtrace();

        /// Removes frames from the top of the stack.
        void skip(unsigned nFrames);

        /// Writes the human-readable backtrace to a stream.
        bool writeTo(std::ostream&) const;
        bool writeTo(crouton::mini::ostream&) const;

        /// Returns the human-readable backtrace.
        std::string toString() const;

        // Direct access to stack frames:

        struct frameInfo {
            const void* pc;         ///< Program counter
            size_t offset;          ///< Byte offset of pc in function
            const char *function;   ///< Name of (nearest) known function
            const char *library;    ///< Name of dynamic library containing the function
            const char *filename;   ///< Name of source file, if known
            uint32_t line;          ///< Line number in source file, if known
        };

        /// The number of stack frames captured.
        size_t size() const;

        /// Returns info about a stack frame. 0 is the top.
        frameInfo getFrame(unsigned) const;
        frameInfo operator[] (unsigned i)       {return getFrame(i);}

        /// Installs a C++ terminate_handler that will log a backtrace and info about any uncaught
        /// exception. By default it then calls the preexisting terminate_handler, which usually
        /// calls abort().
        ///
        /// Since the OS will not usually generate a crash report upon a SIGABORT, you can set
        /// `andRaise` to a different signal ID such as SIGILL to force a crash.
        ///
        /// Only the first call to this function has any effect; subsequent calls are ignored.
        static void installTerminateHandler(std::function<void(const std::string&)> logger);

    private:
        void _capture(unsigned skipFrames =0, unsigned maxFrames =50);
        char* printFrame(unsigned i) const;
        static void writeCrashLog(std::ostream&);

        struct impl;
        std::unique_ptr<impl> _impl;
    };


    /// Attempts to return the unmangled name of the type. (If it fails, returns the mangled name.)
    std::string Unmangle(const std::type_info&);

    /// Attempts to unmangle a name. (If it fails, returns the input string unaltered.)
    std::string Unmangle(const char *name);

    /// Returns the mangled name of the function at the given address, or an empty string if none.
    std::string RawFunctionName(const void *pc);

    /// Returns the name of the function at the given address, or an empty string if none.
    /// The name will be unmangled, if possible.
    std::string FunctionName(const void *pc);

}
