//
// Varint.cc
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

#include "crouton/util/Varint.hh"
#include "crouton/Error.hh"

namespace crouton::uvarint {

    bool readPartial(ConstBytes& bytes, uint64_t* outN) {
        if (bytes.empty())
            return false;
        uint64_t n = 0;
        int shift = 0;
        auto end = std::min(bytes.begin() + kMaxSize, bytes.end());
        auto i = bytes.begin();
        for (; i != end; ++i) {
            if (auto b = uint8_t(*i); b & 0x80) {
                n |= uint64_t(b & 0x7F) << shift;
                shift += 7;
            } else {
                bytes = ConstBytes(i + 1, bytes.end());
                *outN = n | (uint64_t(b) << shift);
                return true;
            }
        }
        if (size_t(i - bytes.begin()) >= kMaxSize)
            Error(CroutonError::ParseError).raise("invalid varint");
        return false;
    }

    uint64_t read(ConstBytes& bytes) {
        uint64_t n;
        if (readPartial(bytes, &n))
            return n;
        else
            Error(CroutonError::ParseError).raise("invalid varint");
    }

    size_t put(uint64_t n, void* dst) {
        uint8_t* i = (uint8_t*)dst;
        while (n >= 0x80) {
            *i++ = (n & 0xFF) | 0x80;
            n >>= 7;
        }
        *i++ = (uint8_t)n;
        return i - (uint8_t*)dst;
    }

}
