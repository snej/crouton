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

    ostream& ostream::write(ConstBytes b)      {return write((const char*)b.data(), b.size());}
    ostream& ostream::write(const char* str)   {return write(str, strlen(str));}
    ostream& ostream::write(string const& str) {return write(str.data(), str.size());}

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

    ostream& ostream::writeFloat(float f) {
        char buf[20];
#if 0 // not available on macOS before 13.3; //TODO: Conditionalize this
        auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], f);
        return write(&buf[0], result.ptr - buf);
#else
        snprintf(buf, sizeof(buf), "%g", f);
        return write(buf);
#endif
    }

    ostream& ostream::writeDouble(double f) {
        char buf[30];
#if 0 // not available on macOS before 13.3; //TODO: Conditionalize this
        auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], f);
        return write(&buf[0], result.ptr - buf);
#else
        snprintf(buf, sizeof(buf), "%g", f);
        return write(buf);
#endif
    }

    ostream& operator<< (ostream& out, const void* ptr) {
        return out.write("0x").writeUInt64(uintptr_t(ptr));
    }


    ostream& fdstream::write(const char* src, size_t len) {
        fwrite(src, len, 1, _fd);
        return *this;
    }

    void fdstream::flush() {
        fflush(_fd);
    }
    
}
