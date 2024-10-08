//
// NWConnection.cc
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

#ifdef __APPLE__

#include "crouton/io/apple/NWConnection.hh"
#include "crouton/Future.hh"
#include "crouton/util/Logging.hh"

#include <Network/Network.h>
#include <Security/SecBase.h>
#include <algorithm>

namespace crouton {
    string ErrorDomainInfo<io::apple::POSIXError>::description(errorcode_t err) {
        return strerror(err);
    }

    string ErrorDomainInfo<io::apple::DNSError>::description(errorcode_t err) {
        return ""; //TODO: I can't find a function in <dns_sd.h> that returns an error message
    }

    string ErrorDomainInfo<io::apple::TLSError>::description(errorcode_t err) {
        CFStringRef cfStr = SecCopyErrorMessageString(err, nullptr);
        if (!cfStr)
            return "";
        char buf[256];
        if (! CFStringGetCString(cfStr, buf, 256, kCFStringEncodingUTF8))
            buf[0] = '\0';
        CFRelease(cfStr);
        return buf;
    }
}

namespace crouton::io::apple {
    using namespace std;


    static Error toError(nw_error_t err) {
        if (int code = nw_error_get_error_code(err)) {
            switch (nw_error_get_error_domain(err)) {
                case nw_error_domain_posix:
                    return POSIXError(code);
                case nw_error_domain_dns:
                    // "mDNS Error codes are in the range FFFE FF00 (-65792) to FFFE FFFF (-65537)"
                    code -= -65537;
                    assert(errorcode_t(code) == code);
                    return DNSError(code);
                case nw_error_domain_tls:
                    return TLSError(code);
                case nw_error_domain_invalid:
                default:
                    break;
            }
        }
        return Error{};
    }


    NWConnection::~NWConnection() {
        _close();
        if (_queue)
            dispatch_release(_queue);
    }

    Future<void> NWConnection::open() {
        string portStr = std::to_string(_binding->port);
        nw_endpoint_t endpoint = nw_endpoint_create_host(_binding->address.c_str(), portStr.c_str());

        nw_parameters_configure_protocol_block_t tlsConfig;
        if (_useTLS)
            tlsConfig = NW_PARAMETERS_DEFAULT_CONFIGURATION;
        else
            tlsConfig = NW_PARAMETERS_DISABLE_PROTOCOL;
        auto params = nw_parameters_create_secure_tcp(tlsConfig,
                                                      NW_PARAMETERS_DEFAULT_CONFIGURATION);
        //TODO: Set nodelay, keepalive based on _binding
        _conn = nw_connection_create(endpoint, params);
        nw_release(endpoint);
        nw_release(params);

        _queue = dispatch_queue_create("NWConnection", DISPATCH_QUEUE_SERIAL);
        nw_connection_set_queue(_conn, _queue);

        FutureProvider<void> onOpen = Future<void>::provider();
        _onClose = Future<void>::provider();
        nw_connection_set_state_changed_handler(_conn, ^(nw_connection_state_t state,
                                                         nw_error_t error) {
            switch (state) {
                case nw_connection_state_ready:
                    _isOpen = true;
                    onOpen->setResult();
                    break;
                case nw_connection_state_cancelled:
                    if (!onOpen->hasResult())
                        onOpen->setResult(CroutonError::Cancelled);
                    _onClose->setResult();
                    break;
                case nw_connection_state_failed:
                    if (!onOpen->hasResult())
                        onOpen->setResult(toError(error));
                    break;
                default:
                    break;
            }
        });
        nw_connection_start(_conn);
        return Future(onOpen);
    }


    Future<void> NWConnection::close() {
        if (_conn) {
            if (!_canceled) {
                LNet->info("NWConnection::close");
                _canceled = true;
                nw_connection_cancel(_conn);
            }
        } else if (!_onClose->hasResult())
            _onClose->setResult();
        return Future(_onClose);
    }


    void NWConnection::_close() {
        if (_conn) {
            if (!_canceled) {
                LNet->warn("NWConnection was not closed -- destructor is forcing cancel");
                nw_connection_set_state_changed_handler(_conn, nullptr);
                nw_connection_force_cancel(_conn);
            }
            nw_release(_conn);
            _conn = nullptr;
        }
    }


    shared_ptr<IStream> NWConnection::stream() {
        return shared_ptr<IStream>(shared_from_this(), static_cast<IStream*>(this));
    }


    void NWConnection::clearReadBuf() {
        if (_content) {
            dispatch_release(_content);
            _content = nullptr;
        }
        _contentBuf = ConstBytes{};
        _contentUsed = 0;
    }


    Future<ConstBytes> NWConnection::readNoCopy(size_t maxLen) {
        return _readNoCopy(maxLen, false);
    }


    Future<ConstBytes> NWConnection::peekNoCopy() {
        return _readNoCopy(65536, true);
    }


    Future<ConstBytes> NWConnection::_readNoCopy(size_t maxLen, bool peek) {
        if (_content && _contentUsed < _contentBuf.size()) {
            // I can return some unRead data from the buffer:
            auto len = std::min(maxLen, _contentBuf.size() - _contentUsed);
            ConstBytes result(_contentBuf.data() + _contentUsed, len);
            _contentUsed += len;
            return result;

        } else if (_eof) {
            return ConstBytes{};
            
        } else {
            // Read from the stream:
            auto onRead = Future<ConstBytes>::provider();
            dispatch_sync(_queue, ^{
                clearReadBuf();
                __block auto selfRetain = this->shared_from_this();
                nw_connection_receive(_conn, 1, uint32_t(min(maxLen, size_t(UINT32_MAX))),
                                      ^(dispatch_data_t content,
                                        nw_content_context_t context,
                                        bool is_complete,
                                        nw_error_t error) {
                    if (is_complete) {
                        LNet->debug("NWConnection read EOF");
                        _eof = true;
                    }
                    if (content) {
                        const void* data;
                        size_t size;
                        _content = dispatch_data_create_map(content, &data, &size);
                        _contentBuf = ConstBytes(data, size);
                        _contentUsed = peek ? 0 : size;
                        LNet->debug("NWConnection read {} bytes", size);
                        onRead->setResult(_contentBuf);
                    } else if (error) {
                        onRead->setResult(toError(error));
                    } else if (is_complete) {
                        onRead->setResult(ConstBytes{});
                    }
                    selfRetain = nullptr;
                });
            });
            return Future(onRead);
        }
    }


    ASYNC<void> NWConnection::closeWrite() {
        LNet->info("NWConnection::closeWrite");
        return _writeOrShutdown({}, true);
    }

    ASYNC<void> NWConnection::write(ConstBytes b) {
        return _writeOrShutdown(b, false);
    }


    Future<void> NWConnection::_writeOrShutdown(ConstBytes src, bool shutdown) {
        __block auto onWrite = Future<void>::provider();
        dispatch_sync(_queue, ^{
            __block __unused bool released = true;
            dispatch_data_t content = nullptr;
            if (src.empty()) {
                assert(shutdown);
            } else {
                released = false;
                content = dispatch_data_create(src.data(), src.size(), _queue,
                                               ^{ released = true; });
            }
            nw_connection_send(_conn, content, NW_CONNECTION_DEFAULT_STREAM_CONTEXT, shutdown,
                               ^(nw_error_t error) {
                assert(released);
                if (error)
                    onWrite->setResult(toError(error));
                else
                    onWrite->setResult();
                onWrite = nullptr;
            });
            if (content)
                dispatch_release(content);
        });
        return Future(onWrite);
    }

}

#endif // __APPLE__
