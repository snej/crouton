//
// Codec.cc
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//


// For zlib API documentation, see: https://zlib.net/manual.html


#include "Codec.hh"
#include "Codec_Impl.hh"
#include "crouton/io/blip/Protocol.hh"
#include "crouton/util/Logging.hh"
#include "support/Endian.hh"
#include <algorithm>
#include <mutex>

namespace crouton::io::blip {
    using namespace std;

    log::logger* LZip;

#ifdef CODEC_USE_ZLIB
    unique_ptr<Codec> Codec::newDeflater(CompressionLevel level) {
        return make_unique<ZlibDeflater>(level);}
    unique_ptr<Codec> Codec::newInflater() {return make_unique<ZlibInflater>();}
#else
    unique_ptr<Codec> Codec::newDeflater(CompressionLevel level) {
        return make_unique<MiniZDeflater>(level);}
    unique_ptr<Codec> Codec::newInflater() {return make_unique<MiniZInflater>();}
#endif

    Codec::Codec()
    :_checksum((uint32_t)crc32(0, nullptr, 0))  // the required initial value
    {
        static once_flag sOnce;
        call_once(sOnce, []{LZip = MakeLogger("Zip");} );
    }

    void Codec::addToChecksum(ConstBytes data) {
        _checksum = (uint32_t)crc32(_checksum, (const Bytef*)data.data(), (int)data.size());
    }

    void Codec::writeChecksum(MutableBytes& output) const {
        uint32_t chk = endian::encodeBig(_checksum);
        __unused bool ok = output.write(&chk, sizeof(chk));
        assert(ok);
    }

    uint32_t Codec::readChecksum(ConstBytes& input) const {
        uint32_t chk;
        static_assert(kChecksumSize == sizeof(chk), "kChecksumSize is wrong");
        if (!input.readAll(&chk, sizeof(chk)))
            Error::raise(ProtocolError::InvalidFrame, "BLIP message ends before checksum");
        return endian::decodeBig(chk);
    }

    void Codec::verifyChecksum(uint32_t expectedChecksum) const {
        if (expectedChecksum != _checksum)
            Error::raise(ProtocolError::BadChecksum);
    }

    void Codec::readAndVerifyChecksum(ConstBytes& input) const {
        verifyChecksum(readChecksum(input));
    }

    // Uncompressed write: just copies input bytes to output (updating checksum)
    MutableBytes Codec::_writeRaw(ConstBytes& input, MutableBytes& output) {
        LZip->debug("Copying {} bytes into {}-byte buf (no compression)", input.size(), output.size());
        assert(output.size() > 0);
        auto outStart = output.data();
        size_t count = output.write(input);
        addToChecksum({input.data(), count});
        input = input.without_first(count);
        return MutableBytes(outStart, count);
    }

    void Codec::writeAll(ConstBytes input, string& output, Mode mode) {
        if (mode == Mode::Raw) {
            output.append(string_view(input));
            addToChecksum(input);
            return;
        }
        size_t outputLen = output.size();
        MutableBytes outBuf, written;
        do {
            if (outputLen == output.size()) {
                // Grow string to make space available to write to:
                size_t bufSize = std::min(std::max(4 * input.size(), size_t(4096)), size_t(32768));
                output.resize(output.size() + bufSize);
            }
            char* outStart = output.data();
            outBuf = MutableBytes(outStart + outputLen, outStart + output.size());
            written = write(input, outBuf, mode);
            outputLen += written.size();
            // Continue until the input is consumed and the codec's run out of output:
        } while (! (input.empty() && outputLen < output.size()) );
        output.resize(outputLen);
    }


#pragma mark - ZLIB IMPLEMENTATION:

#ifdef CODEC_USE_ZLIB


    // "The windowBits parameter is the base two logarithm of the window size (the size of the
    // history buffer)." 15 is the max, and the suggested default value.
    static constexpr int kZlibWindowSize = 15;

    // True to use raw DEFLATE format, false to add the zlib header & checksum
    static constexpr bool kZlibRawDeflate = true;

    // "The memLevel parameter specifies how much memory should be allocated for the internal
    // compression state." Default is 8; we bump it to 9, which uses 256KB.
    static constexpr int kZlibDeflateMemLevel = 9;


    void ZlibCodec::check(int ret) const {
        if (ret < 0 && ret != Z_BUF_ERROR) {
            string msg = minifmt::format("zlib error {}: {}", ret, (_z.msg ? _z.msg : "???"));
            Error::raise(ProtocolError::CompressionError, msg);
        }
    }

    void ZlibCodec::_write(const char* operation, ConstBytes& input, MutableBytes& output, Mode mode,
                           size_t maxInput) {
        _z.next_in  = (Bytef*)input.data();
        auto inSize = _z.avail_in = (unsigned)std::min(input.size(), maxInput);
        _z.next_out = (Bytef*)output.data();
        auto outSize = _z.avail_out = (unsigned)output.size();
        assert(outSize > 0);
        assert(mode > Mode::Raw);
        int result = _flate(&_z, (int)mode);
        LZip->trace("    {}(in {}, out {}, mode {})-> {}; read {} bytes, wrote {} bytes",
                   operation, inSize, outSize,
                   (int)mode, result, (long)(_z.next_in - (uint8_t*)input.data()),
                   (long)(_z.next_out - (uint8_t*)output.data()));
        if (!kZlibRawDeflate) 
            _checksum = (uint32_t)_z.adler;
        input = ConstBytes(_z.next_in, input.endByte());
        output = MutableBytes(_z.next_out, output.endByte());
        check(result);
    }

    
#pragma mark  DEFLATER:


    ZlibDeflater::ZlibDeflater(CompressionLevel level) : ZlibCodec(::deflate) {
        check(::deflateInit2(&_z, level, Z_DEFLATED, kZlibWindowSize * (kZlibRawDeflate ? -1 : 1), kZlibDeflateMemLevel,
                             Z_DEFAULT_STRATEGY));
    }

    ZlibDeflater::~ZlibDeflater() { ::deflateEnd(&_z); }

    MutableBytes ZlibDeflater::write(ConstBytes& input, MutableBytes& output, Mode mode) {
        if (mode == Mode::Raw) 
            return _writeRaw(input, output);

        auto outStart = output.data();
        ConstBytes  origInput = input;
        size_t origOutputSize = output.size();
        LZip->debug("Compressing {} bytes into {}-byte buf with zlib", input.size(), origOutputSize);

        switch (mode) {
            case Mode::NoFlush:
                _write("deflate", input, output, mode);
                break;
            case Mode::SyncFlush:
            case Mode::Finish:
                _writeAndFlush(input, output);
                break;
            default:
                Error::raise(CroutonError::InvalidArgument, "invalid Codec mode");
        }

        if (kZlibRawDeflate) 
            addToChecksum({origInput.data(), input.data()});

        LZip->trace("    compressed {} bytes to {} ({}%), {} unflushed",
                    (origInput.size() - input.size()),
                    (origOutputSize - output.size()),
                    (origOutputSize - output.size()) * 100 / (origInput.size() - input.size()),
                    unflushedBytes());
        return MutableBytes(outStart, output.data());
    }

    void ZlibDeflater::_writeAndFlush(ConstBytes& input, MutableBytes& output) {
        // If we try to write all of the input, and there isn't room in the output, the zlib
        // codec might end up with buffered data that hasn't been output yet (even though we
        // told it to flush.) To work around this, write the data gradually and stop before
        // the output fills up.
        static constexpr size_t kHeadroomForFlush = 12;
        static constexpr size_t kStopAtOutputSize = 100;

        Mode curMode = Mode::PartialFlush;
        while (input.size() > 0) {
            if (output.size() >= deflateBound(&_z, (unsigned)input.size())) {
                // Entire input is guaranteed to fit, so write it & flush:
                curMode = Mode::SyncFlush;
                _write("deflate", input, output, Mode::SyncFlush);
            } else {
                // Limit input size to what we know can be compressed into output.
                // Don't flush, because we may try to write again if there's still room.
                _write("deflate", input, output, curMode, output.size() - kHeadroomForFlush);
            }
            if (output.size() <= kStopAtOutputSize)
                break;
        }

        if (curMode != Mode::SyncFlush) {
            // Flush if we haven't yet (consuming no input):
            _write("deflate", input, output, Mode::SyncFlush, 0);
        }
    }

    unsigned ZlibDeflater::unflushedBytes() const {
        unsigned bytes;
        int      bits;
        check(deflatePending(&_z, &bytes, &bits));
        return bytes + (bits > 0);
    }


#pragma mark  INFLATER:


    ZlibInflater::ZlibInflater() : ZlibCodec(::inflate) {
        check(::inflateInit2(&_z, kZlibRawDeflate ? (-kZlibWindowSize) : (kZlibWindowSize + 32)));
    }

    ZlibInflater::~ZlibInflater() { ::inflateEnd(&_z); }

    MutableBytes ZlibInflater::write(ConstBytes& input, MutableBytes& output, Mode mode) {
        if (mode == Mode::Raw) 
            return _writeRaw(input, output);

        LZip->debug("Decompressing {} bytes into {}-byte buf with zlib", input.size(), output.size());
        auto outStart = (uint8_t*)output.data();
        _write("inflate", input, output, mode);
        if (kZlibRawDeflate)
            addToChecksum({outStart, output.data()});

        LZip->trace("    decompressed {} bytes: {}",
                    (long)((uint8_t*)output.data() - outStart),
                    string_view((char*)outStart, (char*)output.data()));
        return MutableBytes(outStart, output.data());
    }

#endif // CODEC_USE_ZLIB


#pragma mark - MINIZ IMPLEMENTATION:


#ifdef CODEC_USE_MINIZ

    MiniZDeflater::MiniZDeflater(CompressionLevel level) {
        int flags = TDEFL_DEFAULT_MAX_PROBES;
        if (level > DefaultCompression) {
            // taken from miniz/example/example5.c:
            // The number of dictionary probes to use at each compression level (0-10).
            static constexpr uint16_t kNumProbes[11] = { 0, 1, 6, 32,  16, 32, 128, 256,  512, 768, 1500 };
            flags = kNumProbes[int(level)];
        }
        int status = tdefl_init(&_state, nullptr, nullptr, flags);
        postcondition(status == TDEFL_STATUS_OKAY);
    }

    MiniZDeflater::~MiniZDeflater() = default;

    MutableBytes MiniZDeflater::write(ConstBytes& input, MutableBytes& output, Mode mode) {
        if (mode == Mode::Raw)
            return _writeRaw(input, output);
        size_t origInputSize = input.size(), origOutputSize = output.size();
        LZip->debug("Compressing {} bytes into {}-byte buf with miniz",
                    origInputSize, origOutputSize);
        auto outputStart = output.data();
        _writeAndFlush(input, output);
        LZip->trace("    compressed {} bytes to {} ({}%) with miniz",
                    (origInputSize - input.size()),
                    (origOutputSize - output.size()),
                    (origOutputSize - output.size()) * 100 / (origInputSize - input.size()));
        return {outputStart, output.data()};
    }

    void MiniZDeflater::_writeAndFlush(ConstBytes& input, MutableBytes& output) {
        // If we try to write all of the input, and there isn't room in the output, the zlib
        // codec might end up with buffered data that hasn't been output yet (even though we
        // told it to flush.) To work around this, write the data gradually and stop before
        // the output fills up.
        static constexpr size_t kHeadroomForFlush = 12;
        static constexpr size_t kStopAtOutputSize = 100;

        Mode curMode = Mode::PartialFlush;
        while (input.size() > 0) {
            if (output.size() >= input.size() + 12) {
                // Entire input is guaranteed to fit, so write it & flush:
                curMode = Mode::SyncFlush;
                _write(input, output, Mode::SyncFlush);
            } else {
                // Limit input size to what we know can be compressed into output.
                // Don't flush, because we may try to write again if there's still room.
                _write(input, output, curMode, output.size() - kHeadroomForFlush);
            }
            if (output.size() <= kStopAtOutputSize)
                break;
        }

        if (curMode != Mode::SyncFlush) {
            // Flush if we haven't yet (consuming no input):
            _write(input, output, Mode::SyncFlush, 0);
        }
    }


    void MiniZDeflater::_write(ConstBytes& input, MutableBytes& output,
                                       Mode mode, size_t maxInput)
    {
        static constexpr tdefl_flush kModeToFlush[5] = {
            TDEFL_NO_FLUSH, TDEFL_SYNC_FLUSH, TDEFL_SYNC_FLUSH, TDEFL_FULL_FLUSH, TDEFL_FINISH
        };
        precondition(int(mode) >= 0 && int(mode) < 5);
        size_t inSize = std::min(input.size(), maxInput);
        size_t origInSize = inSize;
        size_t outSize = output.size();

        auto status = tdefl_compress(&_state, input.data(), &inSize,
                                     output.data(), &outSize, kModeToFlush[int(mode)]);
        LZip->trace("    deflate(in {}, out {}, mode {}); read {} bytes, wrote {} bytes",
                    origInSize, output.size(), int(mode), inSize, outSize);
        if (status == TDEFL_STATUS_BAD_PARAM) {
            Error(CroutonError::InvalidArgument).raise();
        }
        assert(status >= 0);

        addToChecksum({input.data(), inSize});
        input = input.without_first(inSize);
        output = output.without_first(outSize);
    }



    MiniZInflater::MiniZInflater() {
        tinfl_init(&_state);
    }

    MiniZInflater::~MiniZInflater() = default;


    MutableBytes MiniZInflater::write(ConstBytes& input, MutableBytes& output, Mode mode) {
        if (mode == Mode::Raw)
            return _writeRaw(input, output);

        LZip->debug("Decompressing {} bytes into {}-byte buf with miniz", input.size(), output.size());

        // When my internal output buffer fills up, reset it to empty;
        // but only if the client has received all the data in it:
        bool bufferFull = (_outputPos == sizeof(_outputBuf));
        if (bufferFull && _clientPos == _outputPos) {
            LZip->trace("    recycling output buffer");
            _outputPos = _clientPos = 0;
            bufferFull = false;
        }

        if (!input.empty() && !bufferFull) {
            // Inflate some data:
            uint32_t flags = 0;
            if (mode != Mode::Finish)
                flags |= TINFL_FLAG_HAS_MORE_INPUT;

            size_t availableInBuf = sizeof(_outputBuf) - _outputPos;
            size_t inSize = input.size();
            size_t outSize = availableInBuf;
            auto status = tinfl_decompress(&_state, (const mz_uint8 *)input.data(), &inSize,
                                           _outputBuf,
                                           _outputBuf + _outputPos, &outSize,
                                           flags);
            LZip->trace("    inflate(in {}, out {}) -> {}; read {} bytes, wrote {} bytes",
                       input.size(), availableInBuf, int(status), inSize, outSize);
            if (status < 0) {
                Error err;
                switch (status) {
                    case TINFL_STATUS_BAD_PARAM: err = CroutonError::InvalidArgument; break;
                    case TINFL_STATUS_ADLER32_MISMATCH: err = ProtocolError::CompressionError; break;
                    default: err = CroutonError::ParseError; break;
                }
                err.raise();
            }
            input = input.without_first(inSize);
            _outputPos += outSize;
        }

        MutableBytes result;
        if (size_t bytesAvailable = _outputPos - _clientPos; bytesAvailable > 0) {
            // Write some data to the client's output buffer:
            result = output;
            size_t resultSize = output.write(_outputBuf + _clientPos, bytesAvailable);
            result = result.first(resultSize);
            _clientPos += resultSize;
            addToChecksum(result);
            LZip->trace("    copied {} of {} available bytes to output",
                        result.size(), bytesAvailable);
        }
        return result;
    }


    unsigned MiniZInflater::unflushedBytes() const {
        return _outputPos - _clientPos;
    }


#endif // CODEC_USE_MINIZ

}
