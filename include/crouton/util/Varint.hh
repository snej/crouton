//
// Varint.hh
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

#pragma once
#include "crouton/util/Bytes.hh"

namespace crouton::uvarint {

    // These functions read and write 64-bit unsigned "varints" (variable-length integers)
    // in the format used by Protocol Buffers, Go, and BLIP:
    // - Each byte contains 7 bits of the number, starting with the least significant.
    // - Each byte except the last has its high bit set.

    /// The maximum size in bytes of a varint.
    constexpr size_t kMaxSize = 10;

    /// Decodes a varint from `bytes` and returns it. Moves the start of `bytes` past the varint.
    /// @throws `CroutonError::ParseError` if a complete varint cannot be read.
    uint64_t read(ConstBytes& bytes);

    /// Decodes a varint from `bytes` and returns it. Moves the start of `bytes` past the varint.
    /// If `bytes` contains only a prefix of a varint, returns `false` instead of throwing.
    /// @throws `CroutonError::ParseError` if the data format is invalid, i.e. 10 or more bytes
    ///     with their high bit set.
    bool readPartial(ConstBytes& bytes, uint64_t* outN);

    /// Writes `n` as a varint at `dst`, returning the number of bytes written.
    size_t put(uint64_t n, void* dst);

    /// Writes `n` as a varint at the start of `out`. Moves the start of `out` past the varint.
    /// @warning  Does not range check, so `out` must be large enough! 10 bytes will suffice.
    inline void write(uint64_t n, MutableBytes& out) {
        out = out.without_first(put(n, out.data()));
    }

}
