//
// MiniOStream.cc
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
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

#include "crouton/util/MiniOStream.hh"
#include <charconv>
#include <cstdio>

namespace crouton::mini {
    

    fdstream cout(stdout);
    fdstream cerr(stderr);

    ostream& ostream::write(std::span<const char> b){return write(b.data(), b.size());}
    ostream& ostream::write(std::span<const std::byte> b){return write((const char*)b.data(), b.size());}
    ostream& ostream::write(const char* str)   {return write(str, strlen(str));}
    ostream& ostream::write(std::string const& str) {return write(str.data(), str.size());}

    ostream& ostream::writeInt64(int64_t i, int base) {
        char buf[20];
        auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], i, base);
        return write(&buf[0], size_t(result.ptr - buf));
    }

    ostream& ostream::writeUInt64(uint64_t i, int base) {
        char buf[20];
        auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], i, base);
        return write(&buf[0], result.ptr - buf);
    }

    ostream& ostream::writeDouble(double f) {
        char buf[30];
#ifdef __APPLE__ // Apple's libc++ didn't add this method until later
        if (__builtin_available(macOS 13.3, iOS 16.3, tvOS 16.3, watchOS 9.3, *)) {
#endif
            auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], f);
            return write(&buf[0], result.ptr - buf);
#ifdef __APPLE__
        } else {
            snprintf(buf, sizeof(buf), "%g", f);
            return write(buf);
        }
#endif
    }

    ostream& operator<< (ostream& out, const void* ptr) {
        return out.write("0x").writeUInt64(uintptr_t(ptr), 16);
    }


    ostream& fdstream::write(const char* src, size_t len) {
        fwrite(src, len, 1, _fd);
        return *this;
    }

    void fdstream::flush() {
        fflush(_fd);
    }
    
}
