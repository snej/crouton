//
// TCPSocket.hh
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
#include "crouton/io/Stream.hh"


struct uv_tcp_s;
namespace crouton::io {
    class TCPServer;
}

namespace crouton::io::uv {

    /** A TCP socket. (For TLS connections, use TLSSocket or NWConnection.) */
    class TCPSocket : public ISocket, private Stream {
    public:
        static std::shared_ptr<TCPSocket> create()  {return std::make_shared<TCPSocket>();}

        /// Opens the socket to the bound address. Resolves once opened.
        ASYNC<void> open() override;

        std::shared_ptr<IStream> stream() override;
        bool isOpen() const override;
        ASYNC<void> close() override;

        TCPSocket();
    protected:
        friend class crouton::io::TCPServer;

        /// Called by TCPServer to create a TCPSocket for a new client connection.
        virtual void accept(uv_tcp_s* handle)       {opened((uv_stream_s*)handle);}
    };
}
