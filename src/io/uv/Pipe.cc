//
// Pipe.cc
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

#include "crouton/io/Pipe.hh"
#include "crouton/Future.hh"
#include "UVInternal.hh"

namespace crouton::io {
    using namespace std;
    using namespace crouton::io::uv;

    pair<Pipe::Ref,Pipe::Ref> Pipe::createPair() {
        uv_file fds[2];
        check(uv_pipe(fds, UV_NONBLOCK_PIPE, UV_NONBLOCK_PIPE), "creating pipes");
        return { make_shared<Pipe>(fds[0]), make_shared<Pipe>(fds[1]) };
    }


    Future<void> Pipe::open() {
        if (_fd >= 0) {
            auto pipe = new uv_pipe_t;
            int err = uv_pipe_init(curLoop(), pipe, false);
            if (err == 0)
                err = uv_pipe_open(pipe, _fd);
            if (err) {
                closeHandle(pipe);
                return Error(UVError(err), "opening a pipe");
            }
            _fd = 0;
            opened((uv_stream_t*)pipe);
        }
        return Future<void>();
    }

}
