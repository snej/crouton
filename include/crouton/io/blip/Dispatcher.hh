//
// Dispatcher.hh
//
// 
//

#pragma once
#include "crouton/io/blip/Message.hh"
#include <functional>
#include <unordered_map>

namespace crouton::io::blip {

    /** A map from message Profile strings to handler functions. */
    class Dispatcher {
    public:
        using RequestHandler = std::function<void(MessageInRef)>;
        using RequestHandlerItem = std::pair<const string,RequestHandler>;
        using ErrorHandler = std::function<Message::Error(crouton::Error)>;

        explicit Dispatcher(std::initializer_list<RequestHandlerItem> = {});

        /// Registers a callback that maps a Crouton error to a BLIP error.
        void setErrorHandler(ErrorHandler h)        {_errorHandler = std::move(h);}

        /// Registers a handler for incoming requests with a specific `Profile` property value.
        /// The profile string `"*"` is a wild-card that matches any message.
        void setRequestHandler(string profile, RequestHandler);

        void addRequestHandlers(std::initializer_list<RequestHandlerItem>);

        /// Calls the handler for a message. 
        /// If there is none, responds with a {BLIP,404} error.
        /// If the handler fails, responds with a {BLIP, 500} error.
        void dispatchRequest(MessageInRef);

        Message::Error mapError(Error);

    private:
        std::unordered_map<string,RequestHandler> _handlers;
        ErrorHandler _errorHandler;
    };

}
