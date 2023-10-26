//
// LocalSocket.cc
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

#include "crouton/io/LocalSocket.hh"
#include "UVInternal.hh"

namespace crouton::io {
    using namespace std;
    using namespace crouton::io::uv;


    pair<LocalSocket::Ref,LocalSocket::Ref> LocalSocket::createPair() {
        uv_os_sock_t fds[2];
        check(uv_socketpair(SOCK_STREAM, 0, fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE),
              "creating a SocketPair");
        return { make_shared<LocalSocket>(fds[0]), make_shared<LocalSocket>(fds[1]) };
    }


    Future<void> LocalSocket::open() {
        if (_fd >= 0) {
            auto tcpHandle = new uv_tcp_t;
            uv_tcp_init(curLoop(), tcpHandle);
            if (int err = uv_tcp_open(tcpHandle, _fd)) {
                closeHandle(tcpHandle);
                return Error(UVError(err), "opening a SocketPair");
            }
            _fd = -1;
            opened((uv_stream_t*)tcpHandle);
        }
        return Future<void>();
    }

}
