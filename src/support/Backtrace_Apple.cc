//
// Backtrace_Apple.cc
//
// 
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

/*
 This is a much simpler implementation of Backtrace that doesn't require the cpptrace library.
 It only works on Apple platforms. (It almost works on Linux, except that `dladdr` doesn't
 find names of functions in the main executable, making it useless.)
 */

#ifdef __APPLE__

#include "crouton/util/Backtrace.hh"
#include <iostream>

#include <cxxabi.h>     // for abi::__cxa_demangle()
#include <dlfcn.h>      // for dladdr()
#include <execinfo.h>   // for backtrace()

#include "crouton/util/betterassert.hh"

namespace crouton {
    using namespace std;


    struct Backtrace::impl {
        std::vector<void*> addrs;          // Array of PCs in backtrace, top first
    };


    Backtrace::Backtrace(unsigned skipFrames, unsigned maxFrames) {
        if (maxFrames > 0)
            _capture(skipFrames + 2, maxFrames);
    }


    Backtrace::~Backtrace() = default;


    void Backtrace::_capture(unsigned skipFrames, unsigned maxFrames) {
        _impl = make_unique<impl>();
        auto& addrs = _impl->addrs;
        addrs.resize(++skipFrames + maxFrames);        // skip this frame
        auto n = ::backtrace(&addrs[0], skipFrames + maxFrames);
        addrs.resize(n);
        skip(skipFrames);
    }


    void Backtrace::skip(unsigned nFrames) {
        auto& addrs = _impl->addrs;
        addrs.erase(addrs.begin(), addrs.begin() + min(size_t(nFrames), addrs.size()));
    }


    size_t Backtrace::size() const {return _impl->addrs.size();}


    Backtrace::frameInfo Backtrace::getFrame(unsigned i) const {
        precondition(i < _impl->addrs.size());
        frameInfo frame = { };
        Dl_info info;
        if (dladdr(_impl->addrs[i], &info)) {
            frame.pc = _impl->addrs[i];
            frame.offset = (size_t)frame.pc - (size_t)info.dli_saddr;
            frame.function = info.dli_sname;
            frame.library = info.dli_fname;
            const char *slash = strrchr(frame.library, '/');
            if (slash)
                frame.library = slash + 1;
        }
        return frame;
    }


#pragma mark - SYMBOLS:


    string Unmangle(const char *function) {
        int status;
        size_t unmangledLen;
        char *unmangled = abi::__cxa_demangle(function, nullptr, &unmangledLen, &status);
        if (unmangled && status == 0)
            return unmangled;
        free(unmangled);
        return string(function);
    }


    string Unmangle(const std::type_info &type) {
        return Unmangle(type.name());
    }


    std::string RawFunctionName(const void *pc) {
        Dl_info info = {};
        dladdr(pc, &info);
        return std::string(info.dli_sname ? info.dli_sname : "");
    }


    std::string FunctionName(const void *pc) {
        if (std::string raw = RawFunctionName(pc); !raw.empty())
            return Unmangle(raw.c_str());
        else
            return "";
    }

}

#endif // APPLE
