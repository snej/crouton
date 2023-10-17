//
// ESPTCPSocket.cc
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

// <https://www.nongnu.org/lwip/2_0_x/raw_api.html>

#include "ESPTCPSocket.hh"
#include "ESPBase.hh"
#include "ESPAddrInfo.hh"
#include "CoCondition.hh"
#include "Internal.hh"
#include "Logging.hh"
#include <esp_log.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>

namespace crouton::esp {
    using namespace std;


    TCPSocket::TCPSocket()
    :_tcp(tcp_new())
    {
        if (!_tcp) throw std::bad_alloc();
    }

    TCPSocket::~TCPSocket() {
        if (_tcp) {
            if (tcp_close(_tcp) != ERR_OK)
                tcp_abort(_tcp);
            _tcp = nullptr;
        }
    }

    ASYNC<void> TCPSocket::open() {
        AddrInfo addr = AWAIT AddrInfo::lookup(_binding->address, _binding->port);

        Blocker<err_t> block;
        auto onConnect = [](void *arg, struct tcp_pcb *tpcb, err_t err) -> err_t {
            // this is called on the lwip thread
            ((Blocker<err_t>*)arg)->notify(err);
            return 0;
        };
        tcp_arg(_tcp, &block);
        if (err_t err = tcp_connect(_tcp, &addr.primaryAddress(), _binding->port, onConnect))
            RETURN LWIPError(err);

        Error error;
        err_t err = AWAIT block;
        if (err)
            RETURN LWIPError(err);

        // Initialize read/write callbacks:
        _isOpen = true;
        tcp_arg(_tcp, this);
        tcp_sent(_tcp, [](void *arg, struct tcp_pcb *tpcb, u16_t len) -> err_t {
            return ((TCPSocket*)arg)->_writeCompleted(len);
        });
        tcp_recv(_tcp, [](void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) -> err_t {
            return ((TCPSocket*)arg)->_readCompleted(p, err);
        });
        RETURN error;
    }


    Future<void> TCPSocket::close() {
        precondition(_isOpen);
        err_t err = tcp_close(_tcp);
        _tcp = nullptr;
        _isOpen = false;
        postcondition(!err);
        return Future<void>{};
    }


    Future<void> TCPSocket::closeWrite() {
        // TODO: Implement
        return Future<void>{};
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
            // Advance _inputBuf->used and return the pointer:
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
        //FIXME: Needs a mutex accessing _readBufs?
        if (_readBufs) {
            // Clean up the pbuf I just completed:
            tcp_recved(_tcp, _readBufs->len);   // Acknowledge this pbuf has been read
            pbuf* next = _readBufs->next;
            if (next)
                pbuf_ref(next);
            pbuf_free(_readBufs);
            _readBufs = next;
        }

        if (!_readBufs && !_readErr) {
            // Wait for buffers to arrive:
            LNet->debug("TCPSocket: waiting to receive data...");
            AWAIT _readBlocker;
            LNet->debug("...TCPSocket: received data");
            if (!_readBufs)
                RETURN _readErr;
        }

        if (_readBufs) {
            _inputBuf = {_readBufs->payload, _readBufs->len};
            RETURN _inputBuf;
        } else if (_readErr == CroutonError::EndOfData) {
            RETURN ConstBytes{};
        } else {
            RETURN _readErr;
        }
    }


    int TCPSocket::_readCompleted(::pbuf *pb, int err) {
        // Warning: This is called on the lwip thread.
        //FIXME: Needs a mutex accessing _readBufs?
        if (pb) {
            ESP_LOGI("TCPSocket", "read completed, %u bytes", pb->tot_len);
            if (_readBufs == nullptr)
                _readBufs = pb;                 // Note: I take over the reference to pb
            else
                pbuf_cat(_readBufs, pb);
        } else {
            ESP_LOGI("TCPSocket", "read completed, LWIP error %d", err);
            _readErr = err ? Error(LWIPError(err)) : Error(CroutonError::EndOfData);
        }
        _readBlocker.notify();
        return ERR_OK;
    }


#pragma mark - WRITING:


    Future<void> TCPSocket::write(ConstBytes data) {
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


    int TCPSocket::_writeCompleted(uint16_t len) {
        // Warning: This is called on the lwip thread.
        ESP_LOGI("TCPSocket", "write completed, %u bytes", len);
        _writeBlocker.notify();
        return 0;
    }

}
