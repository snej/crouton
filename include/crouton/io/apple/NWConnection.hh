//
// NWConnection.hh
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
#ifdef __APPLE__

#include "crouton/CroutonFwd.hh"
#include "crouton/io/ISocket.hh"
#include "crouton/io/IStream.hh"
#include "crouton/Error.hh"

struct dispatch_data_s;
struct dispatch_queue_s;
struct nw_connection;
struct nw_error;

namespace crouton::io::apple {

    // The three error domains that Network.framework's NWError can represent:
    enum class POSIXError : errorcode_t { };
    enum class DNSError : errorcode_t { };
    enum class TLSError : errorcode_t { };


    /** A TCP client connection using Apple's Network.framework.
        Supports TLS. */
    class NWConnection final : public ISocket, private IStream {
    public:
        static std::shared_ptr<NWConnection> create()     {return std::make_shared<NWConnection>();}

        void useTLS(bool tls)                               {_useTLS = tls;}

        /// Opens the socket to the bound address. Resolves once opened.
        virtual ASYNC<void> open() override;

        bool isOpen() const override                        {return _isOpen;}

        ASYNC<void> close() override;

        ASYNC<void> closeWrite() override;

        std::shared_ptr<IStream> stream() override;

        explicit NWConnection(bool useTLS =false)           :_useTLS(useTLS) { }
        ~NWConnection();

    private:
        virtual ASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        virtual ASYNC<ConstBytes> peekNoCopy() override;
        ASYNC<void> write(ConstBytes b) override;
        using IStream::write;
        
        virtual ASYNC<ConstBytes> _readNoCopy(size_t maxLen, bool peek);
        ASYNC<void> _writeOrShutdown(ConstBytes, bool shutdown);
        void _close();
        void clearReadBuf();

        nw_connection*      _conn = nullptr;
        dispatch_queue_s*   _queue = nullptr;
        FutureProvider<void> _onClose;
        dispatch_data_s*    _content = nullptr;
        ConstBytes            _contentBuf;
        size_t              _contentUsed;
        bool                _useTLS = false;
        bool                _isOpen = false;
        bool                _canceled = false;
        bool                _eof = false;
    };
}

namespace crouton {
    template <> struct ErrorDomainInfo<io::apple::POSIXError> {
        static constexpr string_view name = "POSIX";
        static string description(errorcode_t);
    };
    template <> struct ErrorDomainInfo<io::apple::DNSError> {
        static constexpr string_view name = "DNS";
        static string description(errorcode_t);
    };
    template <> struct ErrorDomainInfo<io::apple::TLSError> {
        static constexpr string_view name = "Apple TLS";
        static string description(errorcode_t);
    };
}

#endif // __APPLE__
