//
// Codec.hh
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
#include "crouton/util/Bytes.hh"

namespace crouton::io::blip {

    /** Abstract encoder/decoder/compressor/decompressor class. */
    class Codec {
      public:
        // See https://zlib.net/manual.html#Basic for info about modes
        enum class Mode : int {
            Raw     = -1,  // not a zlib mode; means copy bytes w/o compression
            NoFlush = 0,
            PartialFlush,
            SyncFlush,
            FullFlush,
            Finish,
            Block,
            Trees,

            Default = SyncFlush
        };

        enum CompressionLevel : int8_t {
            DefaultCompression = -1,
            NoCompression      = 0,
            FastestCompression = 1,
            BestCompression    = 9,
        };

        /// Creates a Codec that applies the "deflate" algorithm.
        static std::unique_ptr<Codec> newDeflater(CompressionLevel = DefaultCompression);

        /// Creates a Codec that applies the "inflate" algorithm.
        static std::unique_ptr<Codec> newInflater();

        /** Reads data from `input` and writes transformed data to `output`.
            Each Bytes's start is moved forwards past the consumed data. */
        virtual MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) = 0;

        /** Reads all of `input`, appending the output to the string `output`. */
        void writeAll(ConstBytes input, string& output, Mode = Mode::Default);

        /** Number of bytes buffered in the codec that haven't been written to
            the output yet for lack of space. */
        virtual unsigned unflushedBytes() const { return 0; }

        static constexpr size_t kChecksumSize = 4;

        /** Writes the codec's current 4-byte checksum to the output MutableBytes.
            This is a CRC32 checksum of all the unencoded data processed so far. */
        void writeChecksum(MutableBytes& output) const;

        /** Reads a 4-byte checksum from the input slice and compares it with the codec's
            running checksum.
            @throws ProtocolError::BadChecksum on mismatch. */
        void readAndVerifyChecksum(ConstBytes& input) const;

        /** Reads a 4-byte checksum from the input. */
        uint32_t readChecksum(ConstBytes& input) const;

        /** Verifies that the given checksum matches the data read.
            @throws ProtocolError::BadChecksum on mismatch. */
        void verifyChecksum(uint32_t checksum) const;

        virtual ~Codec() = default;

    protected:
        Codec();
        void addToChecksum(ConstBytes data);
        MutableBytes _writeRaw(ConstBytes& input, MutableBytes& output);

        uint32_t _checksum{0};
    };


    /** Null codec that passes bytes through unaltered. (Still tracks the checksum, though.) */
    class NullCodec final : public Codec {
    public:
        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;
    };

}
