//
// Pipe.hh
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

    /** Pipes are **one-directional** streams that come in pairs.
        The data written to one Pipe can be read from the other.
        If you need a bidirectional stream pair, use `LocalSocket`. */
    class Pipe : public Stream {
    public:
        using Ref = std::shared_ptr<Pipe>;

        /// Creates a pair of connected Pipes. The first is the reader, the second is the writer.
        /// @warning  Trying to send data the wrong direction will throw a broken-pipe error.
        static std::pair<Ref,Ref> createPair();

        ASYNC<void> open() override;

        // private by convention
        explicit Pipe(int fd) :_fd(fd) { }

    private:
        int _fd;
    };

}
