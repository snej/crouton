//
// Framer.hh
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
#include "crouton/Future.hh"
#include "crouton/io/IStream.hh"

namespace crouton::io {

    /** Sends and receives opaque messages over a stream.
        A message is an arbitrary series of bytes, of any length.
        On the stream each message is prefixed with its length as a varint. */
    class Framer {
    public:
        explicit Framer(IStream& stream)   :_stream(stream) { }

        /** A Generator that yields messages until EOF.
            Each yielded `ConstBytes` is valid until the Generator is next awaited. */
        crouton::Generator<ConstBytes> receiveMessages();

        /** Writes a message to the stream.
            @warning  The bytes must remain valid until completion. */
        ASYNC<void> sendMessage(ConstBytes);

        ASYNC<void> closeWrite()            {return _stream.closeWrite();}
        ASYNC<void> close()                 {return _stream.close();}

    private:
        IStream& _stream;
        byte     _buf[10];
        bool     _busy = false;
    };

}
