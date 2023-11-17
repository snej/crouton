//
// Connection.hh
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
#include "crouton/io/blip/BLIPIO.hh"
#include "crouton/io/blip/Dispatcher.hh"
#include "crouton/CoCondition.hh"
#include "crouton/Task.hh"
#include "crouton/io/WebSocket.hh"

#include <optional>

namespace crouton::io::blip {
    class MessageBuilder;


    /** A BLIP WebSocket connection. Glues a `BLIPIO` to a `WebSocket`.
        You should first create and connect the ClientWebSocket or ServerWebSocket,
        then pass it to the Connection constructor, then call `start`. */
    class Connection : private Dispatcher {
    public:
        /// Constructs a Connection and registers any given request handlers.
        explicit Connection(std::unique_ptr<ws::WebSocket> ws,
                            bool enableCompression = true,
                            std::initializer_list<RequestHandlerItem> = {});
        ~Connection();

        /// Registers a handler for incoming requests with a specific `Profile` property value.
        /// The profile string `"*"` is a wild-card that matches any message.
        void setRequestHandler(string profile, RequestHandler h) {
            Dispatcher::setRequestHandler(profile, h);
        }

        /// Begins listening for incoming messages and sending outgoing ones.
        /// You should register your request handlers before calling this.
        void start();

        /// Queues a request to be sent over the WebSocket.
        /// The result resolves to the reply message when it arrives.
        /// If this message is NoReply, the result resolves to `nullptr` when it's sent.
        ASYNC<MessageInRef> sendRequest(MessageBuilder&);

        /// Initiates the close protocol:
        /// 1. Sends all currently queued messages (but no more can be sent)
        /// 2. Sends a WebSocket CLOSE frame with the given code/message
        /// 3. Processes all remaining incoming frames/messages from the peer
        /// 4. When peer's WebSocket CLOSE frame is received, closes the socket.
        ///
        /// If `immediate` is true, step 1 is skipped.
        ASYNC<void> close(ws::CloseCode = ws::CloseCode::Normal,
                          string message = "",
                          bool immediate = false);

    private:
        Task outputTask();
        Task inputTask();

        BLIPIO                          _io;
        std::unique_ptr<ws::WebSocket>  _socket;
        std::optional<Task>             _outputTask, _inputTask;
    };

}
