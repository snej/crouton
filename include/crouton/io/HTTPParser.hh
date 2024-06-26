//
// HTTPParser.hh
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
#include "crouton/Error.hh"
#include "crouton/io/URL.hh"

#include <optional>
#include <unordered_map>

struct llhttp_settings_s;
struct llhttp__internal_s;
namespace crouton::io {
    class IStream;
}

namespace crouton::io::http {

    /// HTTP response status codes. Can be used as an ErrorDomain.
    enum class Status : int {
        Unknown = 0,
        SwitchingProtocols = 101,
        OK = 200,
        MovedPermanently = 301,
        BadRequest = 400,
        Forbidden = 403,
        NotFound = 404,
        MethodNotAllowed = 405,
        ServerError = 500,
    };

    /// HTTP request methods.
    enum class Method : uint8_t {     // !!! values must match enum `llhttp_method` in llhttp.h
        DELETE = 0,
        GET,
        HEAD,
        POST,
        PUT,
        CONNECT,
        OPTIONS,
    };

    ostream& operator<< (ostream&, Status);
    ostream& operator<< (ostream&, Method);


    /// A map of HTTP header names->values. Inherits from `unordered_map`.
    class Headers : public std::unordered_map<string,string> {
    public:
        using unordered_map::unordered_map;

        /// True if the header name exists. Name lookup is case-insensitive.
        bool contains(string const& name) const {
            return find(canonicalName(name)) != end();
        }

        /// Returns the value of a header. Name lookup is case-insensitive.
        string get(string const& name) const {
            auto i = find(canonicalName(name));
            return (i != end()) ? i->second : "";
        }

        /// Sets a header, replacing any prior value. The name is canonicalized.
        void set(string const& name, string const& value) {
            (*this)[canonicalName(name)] = value;
        }

        /// Sets a header, appending to any prior value (with a comma as a delimiter.)
        /// The name is canonicalized.
        void add(string const& name, string const& value) {
            if (auto [i, added] = insert({canonicalName(name), value}); !added) {
                i->second += ", ";
                i->second += value;
            }
        }

        /// Title-capitalizes a header name, e.g. `conTent-TYPe` -> `Content-Type`.
        static string canonicalName(string name);
    };


    /** A class that reads an HTTP request or response from a stream; identifies the metadata
     like method, status headers; and decodes the body if any. */
    class Parser {
    public:
        /// Identifies whether a request or response is to be parsed.
        enum Role {
            Request,
            Response
        };

        /// Constructs a parser that will read from a IStream.
        explicit Parser(IStream& stream, Role role)     :Parser(&stream, role) { }

        /// Constructs a parser that will be fed data by calling `parseData`.
        explicit Parser(Role role)                      :Parser(nullptr, role) { }

        Parser(Parser&&) noexcept;
        Parser& operator=(Parser&&) noexcept;
        ~Parser();

        /// Reads from the stream until the request headers are parsed.
        /// The `status`, `statusMessage`, `headers` fields are not populated until this occurs.
        ASYNC<void> readHeaders();

        /// Low-level method, mostly for testing, that feeds data to the parser.
        /// Returns true if the status and headers are available.
        bool parseData(ConstBytes);

        /// Returns true if the entire request has been read.
        bool complete() const noexcept Pure             {return _messageComplete;}

        /// Returns true if the connection has been upgraded to another protocol.
        bool upgraded() const noexcept Pure             {return _upgraded;}

        //---- Metadata

        /// The HTTP request method.
        Method requestMethod;

        /// The HTTP request URI (path + query)
        std::optional<URL> requestURI;

        /// The HTTP response status code.
        Status status = Status::Unknown;

        /// The HTTP response status message.
        string statusMessage;

        /// All the HTTP headers.
        Headers headers;

        /// Returns the value of an HTTP header. (Case-insensitive.)
        string_view getHeader(const char* name);

        //---- Body

        /// Reads from the response body and returns some more data.
        /// `readHeaders` MUST have completed before you call this.
        /// On EOF returns an empty string.
        ASYNC<string> readBody();

        /// Reads and returns the entire body.
        /// If readBody() has already been called, this will return the remainder.
        ASYNC<string> entireBody();

        /// After a call to parseData, returns body bytes that were read by the call.
        string latestBodyData()    {string b(std::move(_body)); _body.clear(); return b;}

    private:
        Parser(IStream*, Role role);
        int gotBody(const char* data, size_t length);
        int addHeader(string const& value);

        using SettingsRef = std::unique_ptr<llhttp_settings_s>;
        using ParserRef   = std::unique_ptr<llhttp__internal_s>;

        IStream*    _stream;                    // Input Stream, if any
        Role        _role;                      // Request or Response
        SettingsRef _settings;                  // llhttp settings
        ParserRef   _parser;                    // llhttp parser
        string      _statusMsg;                 // Parsed status message ("OK", etc.)
        string      _curHeaderName;             // Latest header name read during parsing
        string      _body;                      // Latest chunk of body read
        bool        _headersComplete = false;   // True when metadata/headers have been read
        bool        _messageComplete = false;   // True when entire request/response is read
        bool        _upgraded = false;          // True on protocol upgrade (WebSocket etc.)
    };

}

template <> struct crouton::ErrorDomainInfo<crouton::io::http::Status> {
    static constexpr string_view name = "HTTP";
    static string                description(errorcode_t);
};
