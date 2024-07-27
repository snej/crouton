//
// Backtrace_cpptrace.cc
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

#include "crouton/util/Backtrace.hh"
#include "cpptrace/cpptrace.hpp"
#include <cstring>
#include <dlfcn.h>      // for dladdr()

namespace crouton {
    using namespace std;


    struct Backtrace::impl {
        cpptrace::stacktrace trace;
        explicit impl(cpptrace::stacktrace t)    :trace(std::move(t)) { }
    };


    Backtrace::Backtrace(unsigned skipFrames, unsigned maxFrames) {
        if (maxFrames > 0)
            _capture(skipFrames + 3, maxFrames);
    }


    Backtrace::~Backtrace() = default;


    void Backtrace::_capture(unsigned skipFrames, unsigned maxFrames) {
        _impl = make_unique<impl>(cpptrace::stacktrace::current(skipFrames + 1, maxFrames));
    }


    size_t Backtrace::size() const {return _impl->trace.frames.size();}


    void Backtrace::skip(unsigned nFrames) {
        auto& frames = _impl->trace.frames;
        frames.erase(frames.begin(), frames.begin() + min(size_t(nFrames), frames.size()));
    }


    Backtrace::frameInfo Backtrace::getFrame(unsigned i) const {
        auto& frame = _impl->trace.frames.at(i);
        frameInfo info {
            .pc       = (const void*)frame.raw_address,
            .offset   = frame.raw_address - frame.object_address,
            .function = frame.symbol.c_str(),
            .filename = frame.filename.c_str(),
            .line     = frame.line.value_or(0)
        };
        const char *slash = strrchr(info.filename, '/');
        if (slash)
            info.filename = slash + 1;
        return info;
    }


#pragma mark - SYMBOLS:


    string Unmangle(const char *name) {
        return cpptrace::demangle(string(name));
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
