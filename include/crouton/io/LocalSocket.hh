//
// LocalSocket.hh
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
#include "crouton/io/Stream.hh"

namespace crouton::io {

    /** LocalSockets are bidirectional streams that come in pairs.
        The data written to either one can be read from the other. */
    class LocalSocket : public Stream {
    public:
        using Ref = std::shared_ptr<LocalSocket>;

        /// Creates a pair of connected bidirectional Streams.
        static std::pair<Ref,Ref> createPair();

        ASYNC<void> open() override;

        // private by convention
        explicit LocalSocket(int fd) :_fd(fd) { }

    private:
        int _fd;
    };

}
