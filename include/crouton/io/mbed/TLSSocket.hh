//
// TLSSocket.hh
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
#include "crouton/io/ISocket.hh"
#include "crouton/io/IStream.hh"


namespace crouton {
    struct Buffer;
}

namespace crouton::io::mbed {

    /*** A TCP socket with TLS, using mbedTLS. */
    class TLSSocket : public ISocket, private IStream {
    public:
        static std::shared_ptr<TLSSocket> create()  {return std::make_shared<TLSSocket>();}


        bool isOpen() const override;
        ASYNC<void> open() override;
        ASYNC<void> close() override;
        ASYNC<void> closeWrite() override;

        std::shared_ptr<IStream> stream() override;

        TLSSocket();
        ~TLSSocket();
    private:
        ASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        ASYNC<ConstBytes> peekNoCopy() override;
        ASYNC<void> write(ConstBytes) override;
        using IStream::write;

        ASYNC<ConstBytes> _readNoCopy(size_t maxLen, bool peek);

        class Impl;
        std::unique_ptr<Impl>   _impl;
        std::unique_ptr<Buffer> _inputBuf;
        bool                    _busy = false;
    };

}
