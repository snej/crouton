//
// ISocket.cc
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

#include "crouton/io/ISocket.hh"
#include "crouton/Future.hh"
#include "crouton/io/mbed/TLSSocket.hh"
#include "io/uv/TCPSocket.hh"

#ifdef __APPLE__
#include "crouton/io/apple/NWConnection.hh"
#endif

#ifdef ESP_PLATFORM
#include "io/esp32/ESPTCPSocket.hh"
#endif

namespace crouton::io {

    std::shared_ptr<ISocket> ISocket::newSocket(bool useTLS) {
#if defined(__APPLE__)
        return std::make_shared<apple::NWConnection>(useTLS);
#elif defined(ESP_PLATFORM)
        if (useTLS)
            return std::make_shared<mbed::TLSSocket>();
        else
            return std::make_shared<esp::TCPSocket>();
#else
        if (useTLS)
            return std::make_shared<mbed::TLSSocket>();
        else
            return std::make_shared<uv::TCPSocket>();
#endif
    }


    ASYNC<void> ISocket::connect(string const& address, uint16_t port) {
        bind(address, port);
        return open();
    }


    void closeThenRelease(std::shared_ptr<ISocket> &&sock) {
        // Final release is deferred until the end of the 'then' callback.
        (void) sock->close().then([sock]() mutable {sock.reset();});
    }

}
