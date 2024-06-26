//
// BLIPIO.hh
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#pragma once
#include "crouton/io/blip/Message.hh"
#include "crouton/CroutonFwd.hh"
#include "crouton/Queue.hh"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace crouton::io::blip {
    class Codec;
    class MessageBuilder;
    class MessageOut;

    /** Lower-level transport-agnostic BLIP API. 
        It doesn't care where frames come from or how they get to the other side.
        Usually you'll want to use BLIPConnection instead, for the typical BLIP-over-WebSocket. */
    class BLIPIO {
    public:
        /// Constructor, duh.
        /// @param enableCompression  Pass `false` if you promise not to send any compressed
        ///     messages. (Receiving them is still OK.) Then BLIPIO will refrain from instantiating
        ///     a gzip compressor, and save about 400KB of RAM. Useful on embedded devices.
        BLIPIO(bool enableCompression =true);
        ~BLIPIO();

        /// Queues a request to be sent.
        /// The result resolves to the reply message when it arrives.
        /// If this message is NoReply, the result resolves to `nullptr` when it's sent.
        ASYNC<MessageInRef> sendRequest(MessageBuilder&);

        /// Call this when you're ready to close and have no more requests nor responses to send.
        /// The Generator (`output()`) will yield all remaining frames of already-queued messages,
        /// then end.
        /// @warning  It is illegal to call `sendRequest` after this.
        void closeSend();

        /// Call this when the peer indicates it won't send any more frames, like when it sends a
        /// WebSocket CLOSE frame or closes its output stream or whatever.
        /// Any partially-complete incoming requests will be discarded.
        /// Any pending responses (`Future<MessageIn>`) will immediately resolve to Error messages.
        /// @warning  It is illegal to call `receive` after this.
        void closeReceive();

        /// Tells BLIPIO that the connection is disconnected and no more messages can be sent or
        /// received. This is like calling both `closeSend` and `closeReceive`, plus it discards all
        /// outgoing messages currently in the queue.
        /// @warning This should only be used if the transport has abruptly failed.
        void stop();

        //==== FRAME I/O:

        /// Passes a received BLIP frame to be parsed, possibly resulting in a finished message.
        /// If the message is a response, it becomes the resolved value of the Future returned from
        /// the `sendRequest` call.
        /// If the message is a request, it's returned from this call.
        /// @returns  A completed incoming request for you to handle, or nullptr.
        MessageInRef receive(ConstBytes frame);

        /// A Generator that yields BLIP frames that should be sent to the destination,
        /// i.e. as binary WebSocket messages.
        Generator<string>& output()     {return  _frameGenerator;}

        /// True if there is work for the Generator to do.
        bool hasOutput() const {
            return !_outbox.empty() || !_wayOutBox.empty() || !_icebox.empty(); }

        /// True if requests/responses can be sent (neither `closeWrite` nor `stop` called.)
        bool isSendOpen() const         {return _sendOpen;}

        /// True if messages will still be received (neither `closeRead` nor `stop` called.)
        bool isReceiveOpen() const      {return _receiveOpen;}

    protected:
        using MessageOutRef = std::shared_ptr<MessageOut>;

        friend class MessageIn;
        bool send(MessageOutRef);

    private:
        void _closeRead();
        bool _queueMessage(MessageOutRef);
        void freezeMessage(MessageOutRef);
        void thawMessage(MessageOutRef);
        Generator<string> frameGenerator();
        ConstBytes createNextFrame(MessageOutRef, uint8_t*, Codec&);

        MessageInRef pendingRequest(MessageNo, FrameFlags);
        MessageInRef pendingResponse(MessageNo, FrameFlags);
        void receivedAck(MessageNo, bool isResponse, ConstBytes);

        using MessageMap = std::unordered_map<MessageNo, MessageInRef>;

        /** Queue of outgoing messages; each message gets to send one frame in turn. */
        class Outbox : public AsyncQueue<MessageOutRef> {
        public:
            [[nodiscard]] MessageOutRef findMessage(MessageNo msgNo, bool isResponse) const;
            void requeue(MessageOutRef);
            bool urgent() const;
        };

        std::unique_ptr<Codec>      _inputCodec;        // Decompressor for incoming frames
        Outbox                      _outbox;            // Round-robin queue of msgs being sent
        Outbox                      _wayOutBox;         // Messages waiting to be sent
        std::vector<MessageOutRef>  _icebox;            // Outgoing msgs on hold awaiting ACK
        MessageMap                  _pendingRequests;   // Unfinished incoming requests
        MessageMap                  _pendingResponses;  // Unfinished incoming responses
        MessageNo                   _lastMessageNo {0}; // Last msg# generated
        MessageNo                   _numRequestsReceived {0}; // Max msg# received
        Generator<string>           _frameGenerator;    // The Generator side of `output()`.
        bool                        _enableCompression;
        bool                        _sendOpen = true;   // True until closeSend or stop called
        bool                        _receiveOpen = true;// True until closeReceive or stop called

        // These are just for statistics/metrics:
        size_t          _maxOutboxDepth{0}, _totalOutboxDepth{0}, _countOutboxDepth{0};
        uint64_t        _totalBytesWritten{0}, _totalBytesRead{0};
    };

}
