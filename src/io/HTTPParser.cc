//
// HTTPParser.cc
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

#include "crouton/io/HTTPParser.hh"
#include "crouton/io/IStream.hh"
#include "crouton/Future.hh"
#include "support/StringUtils.hh"
#include "llhttp.h"

#include "crouton/util/MiniOStream.hh"

namespace crouton {
    string ErrorDomainInfo<io::http::Status>::description(errorcode_t code) {
        return llhttp_status_name(llhttp_status_t(code));
    }
}

namespace crouton::io::http {
    using namespace std;


    ostream& operator<< (ostream& out, Status status) {
        return out << llhttp_status_name(llhttp_status_t(status));
    }

    ostream& operator<< (ostream& out, Method method) {
        return out << llhttp_method_name(llhttp_method_t(method));
    }


    string Headers::canonicalName(string name) {
        bool inWord = false;
        for (char &c : name) {
            c = inWord ? toLower(c) : toUpper(c);
            inWord = isAlphanumeric(c);
        }
        return name;
    }

#define SELF ((Parser*)parser->data)

    Parser::Parser(IStream* stream, Role role)
    :_stream(stream)
    ,_role(role)
    ,_settings(make_unique<llhttp_settings_s>())
    ,_parser(make_unique<llhttp_t>())
    {
        llhttp_settings_init(_settings.get());

        _settings->on_status = [](llhttp_t* parser, const char *data, size_t length) -> int {
            SELF->statusMessage = string(data, length);
            return 0;
        };
        _settings->on_url = [](llhttp_t* parser, const char *data, size_t length) -> int {
            SELF->requestURI = URL(string(data, length));
            return 0;
        };
        _settings->on_header_field = [](llhttp_t* parser, const char *data, size_t length) -> int {
            SELF->_curHeaderName = string(data, length);
            return 0;
        };
        _settings->on_header_value = [](llhttp_t* parser, const char *data, size_t length) -> int {
            return SELF->addHeader(string(data, length));
        };
        _settings->on_headers_complete = [](llhttp_t* parser) -> int {
            SELF->_headersComplete = true;
            return 0;
        };
        _settings->on_body = [](llhttp_t* parser, const char *data, size_t length) -> int {
            return SELF->gotBody(data, length);
        };
        _settings->on_message_complete = [](llhttp_t* parser) -> int {
            SELF->_messageComplete = true;
            return 0;
        };

        llhttp_init(_parser.get(),
                    (role == Request ? HTTP_REQUEST : HTTP_RESPONSE),
                    _settings.get());
    }


    Parser::Parser(Parser&&) noexcept = default;
    Parser& Parser::operator=(Parser&&) noexcept = default;


    Parser::~Parser() {
        if (_parser)
            llhttp_reset(_parser.get());
    }


    Future<void> Parser::readHeaders() {
        precondition(_stream);
        if (!_stream->isOpen())
            AWAIT _stream->open();

        while (true) {
            ConstBytes data = AWAIT _stream->readNoCopy();
            if (parseData(data))
                RETURN noerror;
            else if (data.empty())
                RETURN CroutonError::UnexpectedEOF;
        }
    }


    Future<string> Parser::readBody() {
        precondition(_stream);
        while (_body.empty() && !complete()) {
            ConstBytes data = AWAIT _stream->readNoCopy();
            parseData(data);
            if (data.empty() && !complete())
                RETURN CroutonError::UnexpectedEOF;
        }
        RETURN std::move(_body);
    }

    Future<string> Parser::entireBody() {
        string entireBody;
        while (!complete()) {
            entireBody += (AWAIT readBody());
        }
        RETURN entireBody;
    }


    bool Parser::parseData(ConstBytes data) {
        _parser->data = this;
        llhttp_errno_t err;
        if (data.size() > 0)
            err = llhttp_execute(_parser.get(), (const char*)data.data(), data.size());
        else
            err = llhttp_finish(_parser.get());

        if (err != HPE_OK) {
            if (err == HPE_PAUSED_UPGRADE) {
                // We have a (WebSocket) upgrade. Put any data after the request into _body.
                assert_always(llhttp_get_upgrade(_parser.get()) != 0);
                _upgraded = true;
                const char* end = llhttp_get_error_pos(_parser.get());
                assert((byte*)end >= data.data() && (byte*)end <= data.data() + data.size());
                _body = string(end, (char*)data.data() + data.size() - end);
            } else {
                Error::raise(CroutonError::ParseError, llhttp_get_error_reason(_parser.get()));
            }
        }

        if (_headersComplete) {
            if (_role == Role::Request) {
                this->requestMethod = Method{llhttp_get_method(_parser.get())};
            } else {
                this->status = Status{llhttp_get_status_code(_parser.get())};
            }
        }
        return _headersComplete;
    }


    int Parser::addHeader(string const& value) {
        precondition(!_curHeaderName.empty());
        this->headers.add(_curHeaderName, value);
        _curHeaderName = "";
        return 0;
    }


    int Parser::gotBody(const char *data, size_t length) {
        _body.append(data, length);
        return 0;
    }

}
