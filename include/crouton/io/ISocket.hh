//
// ISocket.hh
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
#include "crouton/CroutonFwd.hh"
#include "crouton/util/betterassert.hh"


namespace crouton::io {
    class IStream;

    /** Abstract interface for opening a network connection.
        @note  Should always be created as a shared_ptr. */
    class ISocket : public std::enable_shared_from_this<ISocket> {
    public:

        /// Factory method: Creates a new ISocket instance of a default subclass.
        /// @param useTLS  True for TLS, false for plain TCP.
        static std::shared_ptr<ISocket> newSocket(bool useTLS);

        /// Specifies the address and port to connect to.
        /// @param address  A DNS hostname or numeric IP address
        /// @param port  A TCP port number
        virtual void bind(string const& address, uint16_t port) {
            precondition(!_binding);
            _binding.reset(new binding{address, port});
        }

        /// Sets the TCP nodelay option. Call this after `bind`.
        virtual void setNoDelay(bool b)                 {_binding->noDelay = b;}

        /// Enables TCP keep-alive with the given ping interval. Call this after `bind`.
        virtual void keepAlive(unsigned intervalSecs)   {_binding->keepAlive = intervalSecs;}

        /// Opens the socket to the bound address. Resolves once opened.
        virtual ASYNC<void> open() =0;

        /// Equivalent to bind + open.
        virtual ASYNC<void> connect(string const& address, uint16_t port);

        /// True if the socket is open/connected.
        virtual bool isOpen() const =0;

        /// The socket's data stream.
        virtual std::shared_ptr<IStream> stream() =0;

        virtual ASYNC<void> close() =0;

        virtual ~ISocket() = default;

        struct binding {
            string address;
            uint16_t port;
            bool noDelay = false;
            unsigned keepAlive = 0;
        };

        void bind(binding b)                            {_binding = std::make_unique<binding>(b);}

    protected:
        std::unique_ptr<binding> _binding;
    };

    
    /// Convenience function that calls `close`, waits for completion, then deletes.
    void closeThenRelease(std::shared_ptr<ISocket>&&);

}
