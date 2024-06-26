//
// BLIPIO.cc
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#include "crouton/io/blip/BLIPIO.hh"
#include "Codec.hh"
#include "Internal.hh"
#include "MessageOut.hh"
#include "crouton/Error.hh"
#include "crouton/Future.hh"
#include "crouton/Generator.hh"
#include "crouton/io/HTTPParser.hh"
#include "crouton/util/Logging.hh"
#include "crouton/util/Varint.hh"
#include "support/StringUtils.hh"
#include "crouton/Task.hh"
#include <mutex>
#include <ranges>

namespace crouton {
    string ErrorDomainInfo<io::blip::ProtocolError>::description(errorcode_t code) {
        using enum io::blip::ProtocolError;
        static constexpr NameEntry names[] = {
            {int(InvalidFrame),         "invalid BLIP frame"},
            {int(PropertiesTooLarge),   "message properties too large"},
            {int(CompressionError),     "failed to decompress message"},
            {int(BadChecksum),          "invalid checksum in message"},
        };
        return NameEntry::lookup(code, names);
    }

    string ErrorDomainInfo<io::blip::AppError>::description(errorcode_t code) {
        using enum io::blip::AppError;
        static constexpr NameEntry names[] = {
            {int(NotFound),     "peer didn't recognize the message"},
        };
        string desc = NameEntry::lookup(code, names);
        if (desc.empty())
            desc = ErrorDomainInfo<io::http::Status>::description(code);
        return desc;
    }
}

namespace crouton::io::blip {
    using namespace std;

    /// Maximum amount of metadata added to a frame (MessageNo, flags, checksum)
    static constexpr size_t kMaxFrameOverhead = uvarint::kMaxSize + sizeof(FrameFlags)
                                                    + Codec::kChecksumSize;
    /// Size of regular frame, in bytes.
    static constexpr size_t kDefaultFrameSize = 4096;
    
    /// Larger frame size used when fewer messages are competing.
    static constexpr size_t kBigFrameSize = 32768;

    /// Max number of messages being multiplexed.
    static constexpr size_t kOutboxCapacity = 10;

    /// Logger
    log::logger* LBLIP;


#pragma mark - OUTBOX:


    [[nodiscard]] MessageOutRef BLIPIO::Outbox::findMessage(MessageNo msgNo, bool isResponse) const {
        auto p = find_if([&](const MessageOutRef& msg) {
            return msg->number() == msgNo && msg->isResponse() == isResponse;
        });
        return p ? *p : MessageOutRef{};
    }


    void BLIPIO::Outbox::requeue(MessageOutRef msg) {
        assert(!contains(msg));
        auto i = end();
        if (msg->urgent() && size() > 1) {
            // High-priority gets queued after the last existing high-priority message,
            // leaving one regular-priority message in between if possible:
            const bool isNew = msg->isNew();
            do {
                --i;
                if ((*i)->urgent()) {
                    if ((i + 1) != end())
                        ++i;
                    break;
                } else if (isNew && (*i)->isNew()) {
                    break;
                }
            } while (i != begin());
            ++i;
        }
        LBLIP->debug("Requeuing {} {}...", kMessageTypeNames[msg->type()], msg->number());
        pushBefore(i, msg);  // inserts _at_ position i, before message *i
    }


    bool BLIPIO::Outbox::urgent() const {
        auto first = peek();
        return first && (*first)->urgent();
    }


#pragma mark - BLIPIO:


    BLIPIO::BLIPIO(bool enableCompression)
    :_inputCodec(Codec::newInflater())
    ,_frameGenerator(frameGenerator())
    ,_enableCompression(enableCompression)
    {
        static once_flag sOnce;
        call_once(sOnce, []{LBLIP = MakeLogger("BLIP");} );
    }


    BLIPIO::~BLIPIO() {
        if (_receiveOpen || _sendOpen)
            stop();
    }


    void BLIPIO::closeSend() {
        if (_sendOpen) {
            LBLIP->info("BLIPIO closeWrite");
            _sendOpen = false;
            if (!hasOutput())
                _outbox.close();    // wakes up the Generator so it can yield null
        }
    }


    void BLIPIO::closeReceive() {
        if (_receiveOpen) {
            LBLIP->info("BLIPIO closeRead");
            _closeRead();
        }
    }

    void BLIPIO::_closeRead() {
        _receiveOpen = false;
        auto cancelMsgs = [&](MessageMap& pending) {
            if (!pending.empty()) {
                LBLIP->debug("  ...Notifying {} incoming messages they're canceled", pending.size());
                for (auto& item : pending)
                    item.second->disconnected();
                pending.clear();
            }
        };
        cancelMsgs(_pendingRequests);
        cancelMsgs(_pendingResponses);
    }


    void BLIPIO::stop() {
        LBLIP->info("BLIPIO stopping");
        _receiveOpen = _sendOpen = false;
        if (size_t n = _outbox.size() + _icebox.size() + _wayOutBox.size(); n > 0) {
            LBLIP->info("  ...Notifying {} outgoing messages they're canceled", n);
            for (auto &msg : _outbox)
                msg->disconnected();
            for (auto &msg : _icebox)
                msg->disconnected();
            _icebox.clear();
            for (auto &msg : _wayOutBox)
                msg->disconnected();
        }
        _outbox.close();
        _wayOutBox.close();

        _closeRead();
    }


#pragma mark - SENDING:


    /// Public API to send a new request.
    Future<MessageInRef> BLIPIO::sendRequest(MessageBuilder& mb) {
        auto message = make_shared<MessageOut>(this, mb, MessageNo::None);
        precondition(message->type() == kRequestType);
        send(message);
        return message->onResponse();
    }


    /// Internal API to send a MessageOut: either a request, a response, or an ACK.
    bool BLIPIO::send(MessageOutRef msg) {
        if (msg->urgent() || _outbox.size() < kOutboxCapacity)
            return _queueMessage(std::move(msg));
        else
            return _wayOutBox.push(std::move(msg));
    }


    /// Adds a new message to the outgoing queue.
    bool BLIPIO::_queueMessage(MessageOutRef msg) {
        if (!_sendOpen) {
            LBLIP->warn("Can't send {} {}; socket is closed for writes",
                        kMessageTypeNames[msg->type()], msg->number());
            msg->disconnected();
            return false;
        }
        LBLIP->info("Sending {}", *msg);
        _maxOutboxDepth = max(_maxOutboxDepth, _outbox.size() + 1);
        _totalOutboxDepth += _outbox.size() + 1;
        ++_countOutboxDepth;
        _outbox.requeue(msg);
        return true;
    }


    /** Adds an outgoing message to the icebox (until an ACK arrives.) */
    void BLIPIO::freezeMessage(MessageOutRef msg) {
        LBLIP->debug("Freezing {} {}", kMessageTypeNames[msg->type()], msg->number());
        assert(!_outbox.contains(msg));
        assert(ranges::find(_icebox, msg) == _icebox.end());
        _icebox.push_back(msg);
    }


    /** Removes an outgoing message from the icebox and re-queues it (after ACK arrives.) */
    void BLIPIO::thawMessage(MessageOutRef msg) {
        LBLIP->debug("Thawing {} {}", kMessageTypeNames[msg->type()], msg->number());
        auto i = ranges::find(_icebox, msg);
        assert(i != _icebox.end());
        _icebox.erase(i);
        _outbox.requeue(msg);
    }

    /** Sends the next frame. */
    Generator<string> BLIPIO::frameGenerator() {
        auto frameBuf = make_unique<uint8_t[]>(kMaxFrameOverhead + kBigFrameSize);
        unique_ptr<Codec> outputCodec;
        if (_enableCompression)
            outputCodec = Codec::newDeflater();
        else
            outputCodec = make_unique<NullCodec>();
        
        Generator<MessageOutRef> generator = _outbox.generate();

        LBLIP->debug("Starting frameGenerator loop...");
        while (_sendOpen || hasOutput()) {
            // Await the next message, if any, from the queue:
            Result<MessageOutRef> msgp;
            msgp = AWAIT generator;
            if (!msgp)
                break;  // stop when I close

            ConstBytes frame = createNextFrame(*std::move(msgp), frameBuf.get(), *outputCodec);

            // Now yield the frame from the Generator ...
            // returns once my client has read it and called `co_await` again
            LBLIP->debug("...Writing {} bytes to socket", frame.size());
            _totalBytesWritten += frame.size();

            YIELD string(string_view(frame));
        }
        LBLIP->debug("Frame Generator stopped");
    }


    ConstBytes BLIPIO::createNextFrame(MessageOutRef msg, uint8_t* frameBuf, Codec& outputCodec) {
        // Assign the message number for new requests.
        if (msg->_number == MessageNo::None) {
            _lastMessageNo = _lastMessageNo + 1;
            msg->_number = _lastMessageNo;
        }

        FrameFlags frameFlags;
        // Set up a buffer for the frame contents:
        size_t maxSize = kDefaultFrameSize;
        if (msg->urgent() || !_outbox.urgent())
            maxSize = kBigFrameSize;
        maxSize += kMaxFrameOverhead;

        MutableBytes out(frameBuf, maxSize);
        uvarint::write(uint64_t(msg->_number), out);
        auto flagsPos = (FrameFlags*)out.data();
        out = out.without_first(1);

        // Ask the MessageOut to write data to fill the buffer:
        auto prevBytesSent = msg->_bytesSent;
        msg->nextFrameToSend(outputCodec, out, frameFlags);
        *flagsPos = frameFlags;
        ConstBytes frame(frameBuf, out.data());

        if (LBLIP->should_log(log::level::debug)) {
            LBLIP->debug("    Sending frame: {} {} {}{}{}{}, bytes {}--{}",
                         kMessageTypeNames[frameFlags & kTypeMask], msg->number(),
                         (frameFlags & kMoreComing ? 'M' : '-'),
                         (frameFlags & kUrgent ? 'U' : '-'),
                         (frameFlags & kNoReply ? 'N' : '-'),
                         (frameFlags & kCompressed ? 'Z' : '-'),
                         prevBytesSent, msg->_bytesSent - 1);
        }

        if (!msg->isAck()) {
            if (frameFlags & kMoreComing) {
                // Return message to the queue if it has more frames left to send:
                if (msg->needsAck() && _receiveOpen)
                    freezeMessage(msg);
                else
                    _outbox.requeue(msg);
            } else {
                // Message is complete:
                // If there is a new MessageOut waiting in the cold, lift the velvet rope:
                if (auto newMsg = _wayOutBox.maybePop())
                    _queueMessage(*newMsg);
                // Add response message to _pendingResponses:
                LBLIP->debug("Sent last frame of {}", *msg);
                if (MessageIn* response = msg->createResponse())
                    _pendingResponses.emplace(response->number(), response);
                else
                    msg->noResponse();
            }
        }
        return frame;
    }


#pragma mark - RECEIVING:


    MessageInRef BLIPIO::receive(ConstBytes frame) {
        _totalBytesRead += frame.size();
        MessageNo msgNo = MessageNo(uvarint::read(frame));
        uint64_t f = uvarint::read(frame);
        if (f > 0x80)
            Error::raise(ProtocolError::InvalidFrame, "unknown frame flags");
        FrameFlags flags = FrameFlags(f);

        LBLIP->debug("Received frame: {} {} {}{}{}{}, length {}",
                     kMessageTypeNames[flags & kTypeMask], msgNo,
                     (flags & kMoreComing ? 'M' : '-'),
                     (flags & kUrgent ? 'U' : '-'),
                     (flags & kNoReply ? 'N' : '-'),
                     (flags & kCompressed ? 'Z' : '-'),
                     (long)frame.size());

        MessageInRef msg;
        auto type = (MessageType)(flags & kTypeMask);
        switch (type) {
            case kRequestType:
                if (_receiveOpen)
                    msg = pendingRequest(msgNo, flags);
                break;
            case kResponseType:
            case kErrorType:
                if (_receiveOpen)
                    msg = pendingResponse(msgNo, flags);
                break;
            case kAckRequestType:
            case kAckResponseType:
                receivedAck(msgNo, (type == kAckResponseType), frame);
                break;
            default:
                LBLIP->warn("Unknown BLIP frame type received");
                // For forward compatibility let's just ignore this instead of closing
                break;
        }

        // Append the frame to the message:
        if (msg) {
            MessageIn::ReceiveState state = msg->receivedFrame(*_inputCodec, frame, flags);
            if (type == kRequestType) {
                if (state == MessageIn::kEnd /*|| state == MessageIn::kBeginning*/) {
                    // Message complete!
                    return msg;
                }
            }
        }
        return nullptr;
    }


    /** Returns the MessageIn object for the incoming request with the given MessageNo. */
    MessageInRef BLIPIO::pendingRequest(MessageNo msgNo, FrameFlags flags) {
        MessageInRef msg;
        auto i = _pendingRequests.find(msgNo);
        if (i != _pendingRequests.end()) {
            // Existing request: return it, and remove from _pendingRequests if the last frame:
            msg = i->second;
            if (!(flags & kMoreComing)) {
                LBLIP->debug("REQ {} has reached the end of its frames", msgNo);
                _pendingRequests.erase(i);
            }
        } else if (msgNo == _numRequestsReceived + 1) {
            // New request: create and add to _pendingRequests unless it's a singleton frame:
            _numRequestsReceived = msgNo;
            msg = make_shared<MessageIn>(this, flags, msgNo);
            if (flags & kMoreComing) {
                _pendingRequests.emplace(msgNo, msg);
                LBLIP->debug("REQ {} has more frames coming", msgNo);
            }
        } else {
            string err = mini::format("Bad incoming REQ {} ({})", msgNo,
                                     (msgNo <= _numRequestsReceived ? "already finished" : "too high"));
            Error::raise(ProtocolError::InvalidFrame, err);
        }
        return msg;
    }


    /** Returns the MessageIn object for the incoming response with the given MessageNo. */
    MessageInRef BLIPIO::pendingResponse(MessageNo msgNo, FrameFlags flags) {
        MessageInRef msg;
        auto i = _pendingResponses.find(msgNo);
        if (i != _pendingResponses.end()) {
            msg = i->second;
            if (!(flags & kMoreComing)) {
                LBLIP->debug("RES {} has reached the end of its frames", msgNo);
                _pendingResponses.erase(i);
            }
        } else {
            string err = mini::format("Bad incoming RES {} ({})", msgNo,
                                     (msgNo <= _lastMessageNo ? "no request waiting" : "too high"));
            Error::raise(ProtocolError::InvalidFrame, err);
        }
        return msg;
    }


    /** Handle an incoming ACK message, by unfreezing the associated outgoing message. */
    void BLIPIO::receivedAck(MessageNo msgNo, bool onResponse, ConstBytes body) {
        // Find the MessageOut in either _outbox or _icebox:
        bool frozen = false;
        MessageOutRef msg = _outbox.findMessage(msgNo, onResponse);
        if (!msg) {
            auto i = ranges::find_if(_icebox, [&](auto &m) {
                return m->number() == msgNo && m->isResponse() == onResponse;
            });
            if (i == _icebox.end()) {
                LBLIP->debug("Received ACK of non-current message ({} {})",
                             (onResponse ? "RES" : "REQ"), msgNo);
                return;
            }
            msg = *i;
            frozen = true;
        }

        // Acks have no checksum and don't go through the codec; just read the byte count:
        auto byteCount = uvarint::read(body);
        msg->receivedAck(uint32_t(byteCount));
        if (frozen && !msg->needsAck()) 
            thawMessage(msg);
    }


}
