//
// TCPServer.cc
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

#include "crouton/io/TCPServer.hh"
#include "io/uv/TCPSocket.hh"
#include "UVInternal.hh"
#include "crouton/util/Logging.hh"

namespace crouton::io {
    using namespace std;
    using namespace crouton::io::uv;


    TCPServer::TCPServer(uint16_t port, const char* interfaceAddr)
    :_tcpHandle(new uv_tcp_s)
    {
        if (!interfaceAddr || string_view(interfaceAddr) == "*")
            interfaceAddr = "0.0.0.0";
        sockaddr_in addr = {};
        check(uv_ip4_addr(interfaceAddr, port, &addr), "parsing server interface");
        uv_tcp_init(curLoop(), _tcpHandle);
        _tcpHandle->data = this;
        check(uv_tcp_bind(_tcpHandle, (sockaddr*)&addr, 0), "initializing server");
    }


    TCPServer::~TCPServer() {
        close();
    }


    uint16_t TCPServer::port() {
        sockaddr_storage addr;
        int addrLen = sizeof(addr);
        check(uv_tcp_getsockname(_tcpHandle, (sockaddr*)&addr, &addrLen), "getting server port");
        uint16_t port;
        if (addr.ss_family == AF_INET)
            port = ((sockaddr_in&)addr).sin_port;
        else
            port = ((sockaddr_in6&)addr).sin6_port;
        return ntohs(port);
    }


    void TCPServer::listen(std::function<void(std::shared_ptr<ISocket>)> acceptor) {
        _acceptor = std::move(acceptor);
        check(uv_listen((uv_stream_t*)_tcpHandle, 2, [](uv_stream_t *server, int status) noexcept {
            try {
                ((TCPServer*)server->data)->accept(status);
            } catch (...) {
                LNet->error("Caught unexpected exception in TCPServer::accept");
            }
        }), "starting server");
        _isOpen = true;
    }


    void TCPServer::close() {
        LNet->info("TCPServer closing listener socket");
        _isOpen = false;
        closeHandle(_tcpHandle);
    }


    void TCPServer::accept(int status) {
        if (status < 0) {
            LNet->error("TCPServer::listen failed: error {} {}", status, uv_strerror(status));
            //TODO: Notify the app somehow
        } else {
            auto clientHandle = new uv_tcp_t;
            uv_tcp_init(curLoop(), clientHandle);
            check(uv_accept((uv_stream_t*)_tcpHandle, (uv_stream_t*)clientHandle),
                  "accepting client connection");
            auto client = make_shared<TCPSocket>();
            client->accept(clientHandle);
            _acceptor(std::move(client));
        }
    }

}
