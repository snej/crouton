//
// Framer.cc
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

#include "crouton/io/Framer.hh"
#include "crouton/util/Varint.hh"
#include "Internal.hh"

namespace crouton::io {
    using namespace std;


    Generator<ConstBytes> Framer::receiveMessages() {
        vector<byte> buffer;
        while (true) {
            ConstBytes valid(buffer);
            uint64_t len;
            bool haveLen = uvarint::readPartial(valid, &len);
            if (haveLen && len <= valid.size()) {
                YIELD ConstBytes(valid.data(), len);
                buffer.erase(buffer.begin(), buffer.begin() + (valid.data() - buffer.data() + len));
            } else {
                size_t n = haveLen ? (len - valid.size()) : 1024;
                ConstBytes more = AWAIT _stream.readNoCopy(n);
                if (more.empty())
                    break;
                buffer.insert(buffer.end(), more.begin(), more.end());
            }
        }
    }

    
    ASYNC<void> Framer::sendMessage(ConstBytes msg) {
        NotReentrant nr(_busy);
        ConstBytes frame{_buf, uvarint::put(msg.size_bytes(), _buf)};
        return _stream.write({frame, msg});
    }

}
