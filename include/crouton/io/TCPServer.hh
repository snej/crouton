//
// TCPServer.hh
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
#include "crouton/io/TCPSocket.hh"

#include <functional>

struct uv_tcp_s;

namespace crouton::io {

    class TCPServer {
    public:
        /// Constructs a TCPServer that will listen on the given port,
        /// or if port is 0, on any available port.
        explicit TCPServer(uint16_t port, const char* interfaceAddr =nullptr);
        ~TCPServer();

        using Acceptor = std::function<void(std::shared_ptr<TCPSocket>)>;

        /// Starts the server. Incoming connections will trigger calls to the acceptor function.
        /// @throws  If it's not possible to listen on the specified port.
        void listen(Acceptor);

        /// The port on which the server is listening.
        uint16_t port();

        /// Stops the server from accepting new connections.
        /// Has no effect on currently open connections; those need to be closed individually.
        void close();

        bool isOpen() const                 {return _isOpen;}

    private:
        void accept(int status);
        
        uv_tcp_s*       _tcpHandle;         // Handle for TCP operations
        Acceptor        _acceptor;
        bool            _isOpen = false;
    };

}
