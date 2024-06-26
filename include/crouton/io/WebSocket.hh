//
// WebSocket.hh
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
#include "crouton/util/Bytes.hh"
#include "crouton/Coroutine.hh"
#include "crouton/Generator.hh"
#include "crouton/io/HTTPConnection.hh"
#include "crouton/io/HTTPHandler.hh"

#include <deque>
#include <optional>

namespace uWS {
    template <const bool isServer> class WebSocketProtocol;
    using ClientProtocol = WebSocketProtocol<false>;
    using ServerProtocol = WebSocketProtocol<true>;
}

namespace crouton::io::ws {

    /// Status code in a WebSocket Close message.
    /// Definitions are at <http://tools.ietf.org/html/rfc6455#section-7.4.1>
    ///
    /// Codes marked `[do not send]` in the comments are intended to be returned by a client
    /// API and are never actually sent in a message.
    enum class CloseCode : errorcode_t {
        Normal           = 1000, // Normal close
        GoingAway        = 1001, // Peer has to close, e.g. because host app is quitting
        ProtocolError    = 1002, // Protocol violation: invalid framing data
        DataError        = 1003, // Message type (e.g. Binary) cannot be handled
        NoCode           = 1005, // No status code in close frame [do not send]
        Abnormal         = 1006, // Peer closed socket unexpectedly w/o CLOSE [do not send]
        BadMessageFormat = 1007, // Unparseable message
        PolicyError      = 1008, // Message "violates policy". General purpose error.
        MessageTooBig    = 1009, // Received a message that's too big to handle
        MissingExtension = 1010, // Server didn't provide necessary extension [client only]
        CantFulfill      = 1011, // Server unexpectedly could not fulfil a request [server only]
        TLSError         = 1015, // TLS handshake failed [do not send]
        AppTransient     = 4001, // App-defined transient error
        AppPermanent     = 4002, // App-defined permanent error
        FirstAvailable   = 5000, // First unregistered code for freeform use
    };


    /// A WebSocket message: a string plus a type.
    struct Message : public string {
        /// WebSocket message types (the numeric values are defined by the protocol.)
        enum Type : uint8_t {
            Text   =  1,
            Binary =  2,
            Close  =  8,
            Ping   =  9,
            Pong   = 10,
        };

        using string::string;

        Message::Type type = Binary;

        Message(CloseCode, string_view message);
        CloseCode closeCode() const;            ///< If type==Close, this is the status code
        string_view closeMessage() const;  ///< If type==Close, this is the status message
    };

    ostream& operator<< (ostream&, Message::Type);
    ostream& operator<< (ostream&, Message const&);
    ostream& operator<< (ostream&, CloseCode);


    /** Abstract base class of WebSocket connections. */
    class WebSocket {
    public:
        /// Produces incoming WebSocket binary messages, asynchronously.
        /// - If there's a connection error, the Future will hold it (and throw when resolved.)
        /// - If the peer decides to close the socket, or after you call `close`, a message of
        ///   type `Close` will arrive. No further messages will arrive; don't call receive again.
        Generator<Message> receive();

        /// Sends a binary message, asynchronously.
        /// @note The data is copied and does not need to remain valid after the call.
        ASYNC<void> send(ConstBytes, Message::Type = Message::Binary);
        ASYNC<void> send(Message const& m)   {return send(ConstBytes(m), m.type);}

        /// Returns true once each side has sent a Close message.
        bool readyToClose() const       {return _closeSent && _closeReceived;}

        /// Closes the socket without sending a Close message or waiting to receive a response.
        /// This should only be called after `readyToClose` has returned `true`.
        ASYNC<void> close();

        /// Closes the connection immediately. Not recommended.
        virtual void disconnect();

        virtual ~WebSocket();

    protected:
        template <bool S> friend class uWS::WebSocketProtocol;

        WebSocket() = default;
        virtual size_t formatMessage(void* dst, ConstBytes message, Message::Type) = 0;
        virtual void consume(ConstBytes) = 0;

        bool handleFragment(std::byte*, size_t, size_t,uint8_t, bool);
        void protocolError(string_view logMessage);
        ASYNC<void> handleCloseMessage(Message const& msg);

        IStream*                _stream = nullptr;
        std::deque<Message>     _incoming;
        std::optional<Message>  _curMessage;
        bool                    _closeSent = false;
        bool                    _closeReceived = false;
    };

    

    /** A client WebSocket connection. */
    class ClientWebSocket final : public WebSocket {
    public:
        /// Constructs a WebSocket, but doesn't connect yet.
        explicit ClientWebSocket(string_view urlStr);
        ~ClientWebSocket();

        /// Adds an HTTP request header.
        /// @note You may need to add a `Sec-WebSocket-Protocol` header if the server requires one.
        void setHeader(const char* name, const char* value);

        /// Connects to the server.
        ASYNC<void> connect();

        /// The HTTP response headers.
        http::Headers const& responseHeaders()    {return _responseHeaders;}

        void disconnect() override;

    protected:
        friend class ServerWebSocket;
        static string generateAcceptResponse(const char* key);

    private:
        size_t formatMessage(void* dst, ConstBytes message, Message::Type) override;
        void consume(ConstBytes) override;

        using ProtocolRef = std::unique_ptr<uWS::ClientProtocol>;

        http::Connection  _connection;
        http::Request     _request;
        string          _accept;
        http::Headers     _responseHeaders;
        ProtocolRef     _clientParser;
    };



    /** A server WebSocket connection. To be used in an HTTPHandler route function. */
    class ServerWebSocket final : public WebSocket {
    public:
        ServerWebSocket();
        ~ServerWebSocket();

        /// Returns true if this is a valid WebSocket client request.
        static bool isRequestValid(http::Handler::Request const&);

        /// Handles an HTTP request.
        /// - If it's a valid WebSocket request, it sends the HTTP 101 response and returns true.
        ///   Caller should then call `receive` in a loop and handle messages until client closes.
        /// - If it's not valid, it returns a 400 response and returns false.
        ASYNC<bool> connect(http::Handler::Request const&,
                            http::Handler::Response&,
                            string_view subprotocol = "");

    private:
        size_t formatMessage(void* dst, ConstBytes message, Message::Type) override;
        void consume(ConstBytes) override;

        std::unique_ptr<uWS::ServerProtocol> _serverParser;
    };
}

template <> struct crouton::ErrorDomainInfo<crouton::io::ws::CloseCode> {
    static constexpr string_view name = "WebSocket";
    static string                description(errorcode_t);
};
