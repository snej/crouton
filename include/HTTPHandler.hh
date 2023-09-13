//
// HTTPHandler.hh
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
#include "HTTPParser.hh"
#include "IStream.hh"
#include "ISocket.hh"
#include "Task.hh"
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <tuple>
#include <vector>

namespace crouton {

    /** An HTTP server's connection to a client,
        from which it will read a request and send a response.
        @note  It does not support keep-alive, so it closes the socket after one response. */
    class HTTPHandler {
    public:

        /// An HTTP request as sent to a HandlerFunction function.
        struct Request {
            HTTPMethod  method = HTTPMethod::GET;   ///< The request method
            URL         uri;                        ///< The request URI (path + query.)
            HTTPHeaders headers;                    ///< The request headers.
            string body;                       ///< The request body.
        };


        /// An HTTP response for a HandlerFunction function to define.
        class Response {
        public:
            HTTPStatus  status = HTTPStatus::OK;    ///< Can change this before calling writeToBody
            string statusMessage;              ///< Can change this before calling writeToBody

            /// Adds a response header.
            void writeHeader(string_view name, string_view value);

            /// Writes to the body. After this you can't call writeHeader any more.
            ASYNC<void> writeToBody(string);

            /// The socket's stream. Only use this when bypassing HTTP, e.g. for WebSockets.
            ASYNC<IStream*> rawStream();

        private:
            friend class HTTPHandler;
            Response(HTTPHandler*, HTTPHeaders&&);
            ASYNC<void> finishHeaders();

            HTTPHandler* _handler;
            HTTPHeaders  _headers;
            bool         _sentHeaders = false;
        };


        /// A function that handles a request, writing a response.
        using HandlerFunction = std::function<Future<void>(Request const&, Response&)>;

        /// An HTTP method and path regex, with the function that should be called.
        struct Route {
            HTTPMethod      method = HTTPMethod::GET;
            std::regex      pathPattern;
            HandlerFunction handler;
        };

        /// Constructs an HTTPHandler on a socket, given its routing table.
        explicit HTTPHandler(std::shared_ptr<ISocket>, std::vector<Route> const&);

        /// Reads the request, calls the handler (or writes an error) and closes the socket.
        ASYNC<void> run();

    private:
        ASYNC<void> handleRequest(HTTPHeaders responseHeaders,
                                  HandlerFunction const& handler);
        ASYNC<void> writeHeaders(HTTPStatus status,
                                 string_view statusMsg,
                                 HTTPHeaders const& headers);
        ASYNC<void> writeToBody(string);
        ASYNC<void> endBody();

        std::shared_ptr<ISocket> _socket;
        IStream&                 _stream;
        HTTPParser               _parser;
        std::vector<Route> const&_routes;
    };

}