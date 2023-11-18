//
// Codec_Impl.hh
//
// Copyright 2023-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#pragma once
#include "Codec.hh"

#if __has_include(<zlib.h>)
#  define CODEC_USE_ZLIB
#endif

#define CODEC_USE_MINIZ


#ifdef CODEC_USE_ZLIB
#  include <zlib.h>
#endif

#ifdef CODEC_USE_MINIZ
#  ifdef ESP_PLATFORM
#    include <miniz.h>
#  else
#    include "miniz_tdef.h"
#    include "miniz_tinfl.h"
#  endif
   extern "C" unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len);
#  define crc32(X,Y,Z) mz_crc32(X,Y,Z)
   using Bytef = unsigned char;
#endif // CODEC_USE_MINIZ


namespace crouton::io::blip {

#ifdef CODEC_USE_ZLIB

    /** Abstract base class of Zlib-based codecs Deflater and Inflater */
    class ZlibCodec : public Codec {
    protected:
        using FlateFunc = int (*)(z_stream*, int);

        explicit ZlibCodec(FlateFunc flate) : _flate(flate) {}

        void _write(const char* operation, ConstBytes& input, MutableBytes& output, Mode,
                    size_t maxInput = SIZE_MAX);
        void check(int) const;

        mutable ::z_stream _z{};
        FlateFunc const    _flate;
    };


    /** Compressing codec that performs a zlib/gzip "deflate". */
    class ZlibDeflater final : public ZlibCodec {
    public:
        explicit ZlibDeflater(CompressionLevel = DefaultCompression);
        ~ZlibDeflater() override;

        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;
        unsigned unflushedBytes() const override;

    private:
        void _writeAndFlush(ConstBytes& input, MutableBytes& output);
    };


    /** Decompressing codec that performs a zlib/gzip "inflate". */
    class ZlibInflater final : public ZlibCodec {
    public:
        ZlibInflater();
        ~ZlibInflater() override;

        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;
    };

#endif // CODEC_USE_ZLIB


#ifdef CODEC_USE_MINIZ

    class MiniZDeflater final : public Codec {
    public:
        explicit MiniZDeflater(CompressionLevel = DefaultCompression);
        ~MiniZDeflater();

        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;

    private:
        void _write(ConstBytes& input, MutableBytes& output, Mode,
                    size_t maxInput = SIZE_MAX);
        void _writeAndFlush(ConstBytes& input, MutableBytes& output);

        tdefl_compressor _state;                // Note: This struct is BIG (320KB!)
    };


    class MiniZInflater final : public Codec {
    public:
        MiniZInflater();
        ~MiniZInflater();

        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;
        unsigned unflushedBytes() const override;

    private:
        tinfl_decompressor  _state;             // miniz decompressor state
        int                 _outputPos = 0;     // index of 1st byte to write to in outputBuf
        int                 _clientPos = 0;     // index past last byte returned to client
        mz_uint8            _outputBuf[32768];  // output buffer; miniz requires >= 32KB
    };

#endif // CODEC_USE_MINIZ

}
