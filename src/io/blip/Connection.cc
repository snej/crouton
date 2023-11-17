//
// Connection.cc
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

#include "crouton/io/blip/Connection.hh"
#include "crouton/io/WebSocket.hh"
#include "crouton/Task.hh"

namespace crouton::io::blip {
    using namespace std;


    Connection::Connection(std::unique_ptr<ws::WebSocket> ws,
                           std::initializer_list<RequestHandlerItem> handlers)
    :Dispatcher(handlers)
    ,_socket(std::move(ws))
    { }

    Connection::~Connection() = default;


    void Connection::start() {
        LBLIP->info("Connection starting");
        _outputTask.emplace(outputTask());
        _inputTask.emplace(inputTask());
    }


    Task Connection::outputTask() {
        do {
            Result<string> frame = AWAIT _io.output();
            if (!frame)
                break; // BLIPIO's send side has closed.
            AWAIT _socket->send(*frame, ws::Message::Binary);
        } while (YIELD true);
    }


    Task Connection::inputTask() {
        do {
            Result<ws::Message> frame = AWAIT _socket->receive();
            if (!frame || frame->type == ws::Message::Close) {
                LBLIP->info("Connection received WebSocket CLOSE");
                break;
            }
            MessageInRef msg = _io.receive(*frame);
            if (msg)
                dispatchRequest(std::move(msg));
        } while (YIELD true);
        _io.closeReceive();
    }


    ASYNC<MessageInRef> Connection::sendRequest(MessageBuilder& msg) {
        return _io.sendRequest(msg);
    }


    Future<void> Connection::close(ws::CloseCode code, string message, bool immediate) {
        LBLIP->info("Connection closing with code {} \"{}\"", int(code), message);
        if (immediate)
            _io.stop();
        else
            _io.closeSend();
        auto& j = _outputTask->join();  // workaround for GCC issue
        AWAIT j;
        LBLIP->debug("Connection now sending WebSocket CLOSE...");
        AWAIT _socket->send(ws::Message{code, message});
        auto& j2 = _inputTask->join();  // workaround for GCC issue
        AWAIT j2;
        AWAIT _socket->close();
        RETURN noerror;
    }

}
