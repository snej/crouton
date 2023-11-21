//
// Dispatcher.cc
//
// 
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

#include "crouton/io/blip/Dispatcher.hh"
#include "crouton/util/MiniFormat.hh"

namespace crouton::io::blip {
    using namespace std;


    Dispatcher::Dispatcher(initializer_list<RequestHandlerItem> handlers)
    :_handlers(handlers)
    { }


    void Dispatcher::setRequestHandler(string profile, RequestHandler handler) {
        _handlers[profile] = std::move(handler);
    }


    void Dispatcher::addRequestHandlers(std::initializer_list<RequestHandlerItem> handlers) {
        for (auto &[profile, fn] : handlers)
            _handlers[profile] = std::move(fn);
    }


    void Dispatcher::dispatchRequest(MessageInRef msg) {
        string profile(msg->property("Profile"));
        auto i = _handlers.find(profile);
        if (i == _handlers.end())
            i = _handlers.find("*");
        if (i == _handlers.end()) {
            msg->notHandled();
            return;
        }

        Error err;
        string exceptionMessage;
        try {
            i->second(std::move(msg));
            return;
        } catch (std::exception const& x) {
            exceptionMessage = x.what();
            err = Error(std::current_exception());
        } catch(...) {
            err = Error(std::current_exception());
        }

        LBLIP->error("Error {} `{}` handling BLIP request {}",
                                 err, exceptionMessage, *msg);
        msg->respondWithError(mapError(err));
    }

    
    Message::Error Dispatcher::mapError(Error err) {
        precondition(err);
        if (int appErr = int(err.as<AppError>())) {
            return {"BLIP", appErr};
        } else if (_errorHandler) {
            return _errorHandler(err);
        } else {
            return {"BLIP", 500, "Internal error handling message"};
        }
    }

}
