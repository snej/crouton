//
// ESPTCPSocket.cc
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

// <https://www.nongnu.org/lwip/2_0_x/raw_api.html>

#include "ESPTCPSocket.hh"
#include "ESPBase.hh"
#include "Internal.hh"
#include "crouton/CoCondition.hh"
#include "crouton/Future.hh"
#include "crouton/util/Logging.hh"
#include "crouton/io/AddrInfo.hh"

#include <esp_log.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>

namespace crouton::io::esp {
    using namespace std;
    using crouton::io::AddrInfo;


    TCPSocket::TCPSocket()
    :_tcp(tcp_new())
    {
        if (!_tcp) throw std::bad_alloc();
    }

    TCPSocket::~TCPSocket() {
        if (_tcp) {
            LNet->info("Closing TCPSocket (destructor)");
            if (tcp_close(_tcp) != ERR_OK)
                tcp_abort(_tcp);
            _tcp = nullptr;
        }
        if (_readBufs)
            pbuf_free(_readBufs);
    }

    ASYNC<void> TCPSocket::open() {
        AddrInfo addr = AWAIT AddrInfo::lookup(_binding->address, _binding->port);

        LNet->info("Opening TCP connection to {}:{} ...", _binding->address, _binding->port);
        Blocker<err_t> block;
        auto onConnect = [](void *arg, struct tcp_pcb *tpcb, err_t err) -> err_t {
            // this is called on the lwip thread
            ((Blocker<err_t>*)arg)->notify(err);
            return 0;
        };
        tcp_arg(_tcp, &block);
        err_t err = tcp_connect(_tcp, &addr.primaryAddress(), _binding->port, onConnect);
        if (!err)
            err = AWAIT block;
        if (err) {
            Error error(LWIPError{err});
            LNet->error("...TCP connection failed: {}", error);
            RETURN error;
        }

        // Initialize read/write callbacks:
        _isOpen = true;
        tcp_arg(_tcp, this);
        tcp_sent(_tcp, [](void *arg, struct tcp_pcb *tpcb, u16_t len) -> err_t {
            return ((TCPSocket*)arg)->_writeCallback(len);
        });
        tcp_recv(_tcp, [](void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) -> err_t {
            return ((TCPSocket*)arg)->_readCallback(p, err);
        });
        RETURN noerror;
    }


    Future<void> TCPSocket::close() {
        if (_isOpen) {
            LNet->info("Closing TCPSocket");
            unique_lock lock(_mutex);
            err_t err = tcp_close(_tcp);
            _tcp = nullptr;
            _isOpen = false;
            if (_readBufs) {
                pbuf_free(_readBufs);
                _readBufs = nullptr;
            }
            postcondition(!err);
        }
        return Future<void>{};
    }


    Future<void> TCPSocket::closeWrite() {
        // TODO: Implement
        return CroutonError::Unimplemented;
    }


    shared_ptr<IStream> TCPSocket::stream() {
        return shared_ptr<IStream>(shared_from_this(), static_cast<IStream*>(this));
    }


#pragma mark - READING:


    size_t TCPSocket::bytesAvailable() const noexcept {
        return !_inputBuf.empty() || _readBufs != nullptr;
    }


    bool TCPSocket::isReadable() const noexcept {
        return _isOpen && bytesAvailable() > 0;
    }

    
    Future<ConstBytes> TCPSocket::readNoCopy(size_t maxLen) {
        precondition(isOpen());
        if (!_inputBuf.empty()) {
            return _inputBuf.read(maxLen);
        } else {
            return fillInputBuf().then([this,maxLen](ConstBytes bytes) -> ConstBytes {
                return _inputBuf.read(maxLen);
            });
        }
    }


    Future<ConstBytes> TCPSocket::peekNoCopy() {
        precondition(isOpen());
        if (_inputBuf.empty())
            return fillInputBuf();
        else
            return Future<ConstBytes>(_inputBuf);
    }


    /// Low-level read method that points `_inputBuf` to the next input buffer.
    Future<ConstBytes> TCPSocket::fillInputBuf() {
        precondition(isOpen() && _inputBuf.empty());

        unique_lock lock(_mutex);
        _readBlocker.reset();
        if (_readBufs) {
            // Clean up the pbuf I just completed:
            tcp_recved(_tcp, _readBufs->len);   // Flow control: Acknowledge this pbuf has been read
            pbuf* next = _readBufs->next;
            if (next)
                pbuf_ref(next);
            pbuf_free(_readBufs);
            _readBufs = next;
        }

        if (!_readBufs && !_readErr) {
            // Wait for buffers to arrive. (Don't hold the mutex while waiting!)
            LNet->debug("TCPSocket: waiting to receive data...");
            lock.unlock();
            AWAIT _readBlocker;
            lock.lock();
            LNet->debug("...TCPSocket: end wait");
        }

        assert(_readBufs || _readErr);
        if (_readBufs) {
            _inputBuf = {_readBufs->payload, _readBufs->len};
            LNet->debug("TCPSocket: received {} bytes", _inputBuf.size());
            RETURN _inputBuf;
        } else if (_readErr == CroutonError::UnexpectedEOF) {
            LNet->info("TCPSocket: EOF, no data to read");
            RETURN ConstBytes{};
        } else {
            LNet->error("TCPSocket: error {}", _readErr);
            RETURN _readErr;
        }
    }


    int TCPSocket::_readCallback(::pbuf *pb, int err) {
        // Warning: This is called on the lwip thread.
        unique_lock lock(_mutex);
        LNet->debug("---- entering TCPSocket::_readCallback ...");
        if (pb) {
            LNet->debug("    read completed, {} bytes", pb->tot_len);
            if (_readBufs == nullptr)
                _readBufs = pb;                 // Note: I take over the reference to pb
            else
                pbuf_cat(_readBufs, pb);
        } else if (err) {
            _readErr = Error(LWIPError(err));
            LNet->debug("    read completed: error {}", _readErr);
        } else {
            _readErr = Error(CroutonError::UnexpectedEOF);
            LNet->debug("    read completed: EOF", _readErr);
        }
        assert(_readBufs || _readErr);
        _readBlocker.notify();
        LNet->debug("---- ... exiting TCPSocket::_readCallback");
        return ERR_OK;
    }


#pragma mark - WRITING:


    Future<void> TCPSocket::write(ConstBytes data) {
        precondition(isOpen());
        int flag = TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE;
        while (!data.empty()) {
            auto nextData = data;
            ConstBytes chunk = nextData.read(tcp_sndbuf(_tcp));
            LNet->info("TCPSocket::writing {} bytes", chunk.size());
            if (nextData.empty())
                flag &= ~TCP_WRITE_FLAG_MORE;
            _writeBlocker.reset();
            switch (err_t err = tcp_write(_tcp, chunk.data(), chunk.size(), flag)) {
                case ERR_OK:
                    data = nextData;
                    break;
                case ERR_MEM:
                    LNet->debug("TCPSocket::write blocking...", chunk.size());
                    AWAIT _writeBlocker;
                    LNet->debug("...TCPSocket::write unblocked", chunk.size());
                    break;
                default:
                    LNet->error("TCPSocket::write -- error {}", err);
                    RETURN LWIPError(err);
            }
        }
        RETURN noerror;
    }


    int TCPSocket::_writeCallback(uint16_t len) {
        // Warning: This is called on the lwip thread.
        LNet->debug("TCPSocket write completed, {} bytes", len);
        _writeBlocker.notify();
        return 0;
    }

}
