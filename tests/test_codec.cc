//
// test_codec.cc
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

#include "tests.hh"
#include "io/blip/Codec_Impl.hh"

using namespace crouton::mini;
using namespace crouton::io::blip;


static constexpr string_view kSmallInput = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Donec sed tortor enim. Nullam nec quam sed odio consequat venenatis et id tortor. Phasellus vitae porttitor tortor. Ut eu diam ullamcorper odio egestas pharetra nec quis diam. Nulla quis dolor consectetur sem bibendum posuere. Phasellus commodo ut mauris ac mollis. Aenean volutpat vulputate ultrices. Phasellus varius purus et augue finibus, at ultrices ante volutpat. Mauris luctus massa a tincidunt posuere. Etiam tincidunt, orci in condimentum malesuada, leo sem luctus dolor, sed dapibus ante justo sed libero. Pellentesque id viverra arcu. Duis lorem mi, eleifend eu iaculis at, laoreet at augue. Curabitur condimentum, augue sit amet varius consequat, lorem odio interdum sem, at venenatis nibh ipsum sit amet velit. Proin consectetur ipsum non arcu commodo ultricies.\n\n\
Nunc ac ex in felis commodo bibendum nec et lacus. Ut tincidunt imperdiet nibh id venenatis. Cras risus purus, rhoncus a auctor nec, posuere at enim. Sed laoreet nisi quis sapien pulvinar, in efficitur massa venenatis. Proin sollicitudin odio a lacus mattis, sit amet imperdiet magna imperdiet. Donec pulvinar pretium augue, vel fringilla massa scelerisque vitae. Integer tristique lorem quis fringilla vehicula. Pellentesque dignissim pretium velit, sed interdum tellus tristique at. Cras rhoncus ex quam, sit amet accumsan tellus pretium non. In semper tellus dui, rutrum pulvinar leo pellentesque cursus. Fusce sed ex eleifend, semper nulla a, ultrices arcu. Praesent condimentum, elit eu rutrum ornare, quam purus tempus lacus, non porttitor nisi augue sit amet massa.\n\n\
Sed sit amet interdum sapien, ac efficitur nulla. Suspendisse dignissim suscipit est, non vehicula metus sagittis id. Ut in consectetur diam, eget commodo lorem. Nulla leo odio, euismod vitae ex in, fringilla ornare nulla. Donec purus massa, maximus sit amet finibus eget, rutrum nec velit. Quisque iaculis ex sit amet libero imperdiet euismod facilisis at diam. Aenean venenatis neque et orci pellentesque mattis.\n\n\
Nulla aliquam mauris eu mi vestibulum pellentesque. Aenean non mauris facilisis, tristique ligula ut, auctor libero. Ut vitae tortor quis dui laoreet bibendum. Curabitur lobortis, augue at euismod mattis, odio odio suscipit elit, id vehicula lacus libero sodales magna. In ac nulla nibh. Nullam varius dictum tellus in ultricies. Nunc sit amet massa odio. Donec efficitur risus pulvinar, bibendum quam nec, convallis felis. Vivamus auctor quam nec metus faucibus, vel condimentum diam condimentum. Nam et risus molestie, lacinia lorem vel, volutpat elit.\
Donec maximus erat ligula, nec rutrum ante tristique vel. Integer et lacus elementum, molestie ipsum et, consectetur lacus. Quisque dapibus maximus suscipit. Sed egestas ut est ac ultrices. Aliquam mollis gravida est, vel consequat risus congue eget. In ligula est, condimentum hendrerit mattis accumsan, tempor at libero. Nunc ut leo at velit eleifend semper. Sed ut sem sit amet neque feugiat dapibus. Maecenas sagittis ex ut mi molestie, vel dictum nisi fermentum. Fusce a ex id risus dictum fringilla.";


struct CodecTest {
    std::unique_ptr<Codec> _compressor;

    void init(bool compressZlib) {
        cout << "**** Test compressing with " << (compressZlib ? "zlib" : "miniz") << endl;
        if (compressZlib)
            _compressor = std::make_unique<ZlibDeflater>();
        else
            _compressor = std::make_unique<MiniZDeflater>();
    }

    std::vector<string> compressToFrames(ConstBytes input, size_t frameSize) {
        std::vector<string> compressed;
        size_t compressedSize = 0;
        ConstBytes inputBuf(input);
        auto buffer = std::make_unique<char[]>(frameSize);
        while (!inputBuf.empty()) {
            //cout << "---- Frame " << (compressed.size() + 1) << " ----\n";
            MutableBytes outputBuf(buffer.get(), frameSize - 4);
            auto mode = inputBuf.empty() ? Codec::Mode::Finish : Codec::Mode::SyncFlush;
            MutableBytes written = _compressor->write(inputBuf, outputBuf, mode);
            outputBuf = MutableBytes(written.endByte(), 4);
            _compressor->writeChecksum(outputBuf);
            compressed.emplace_back((char*)written.data(), written.size() + 4);
            compressedSize += written.size() + 4;
        }
        cout << "Compressed " << input.size() << " bytes to " << compressed.size()
        << " frames, of total size " << compressedSize << endl;
        return compressed;
    }


    string decompressFrames(std::vector<string> const& compressed, bool decompressZlib) {
        std::unique_ptr<Codec> decompressor;
        if (decompressZlib)
            decompressor = std::make_unique<ZlibInflater>();
        else
            decompressor = std::make_unique<MiniZInflater>();
        string decompressed;
        //int n = 0;
        for (string frame : compressed) {
            //cout << "---- Frame " << ++n << ": " << frame.size() << " bytes ----\n";
            ConstBytes inputBuf(frame.data(), frame.size() - 4);
            decompressor->writeAll(inputBuf, decompressed);
            inputBuf = ConstBytes(frame.data() + frame.size() - 4, 4);
            decompressor->readAndVerifyChecksum(inputBuf);
        }
        return decompressed;
    }

    void roundTrip(string_view input, size_t frameSize = 4096) {
        std::vector<string> compressed = compressToFrames(input, frameSize);

        cout << "---------- Decompressing with miniz\n";
        string decompressed = decompressFrames(compressed, false);
        CHECK(decompressed == input);

        cout << "---------- Decompressing with zlib\n";
        decompressed = decompressFrames(compressed, true);
        CHECK(decompressed == input);
    }

};


TEST_CASE_METHOD(CodecTest, "Codec small", "[blip][codec]") {
    init(GENERATE(false, true));
    roundTrip(kSmallInput, 1000);
}


TEST_CASE_METHOD(CodecTest, "Codec large compressible", "[blip][codec]") {
    init(GENERATE(false, true));
    roundTrip(ReadFile("/Users/snej/Projects/Couchbase/DataSets/travel.json").waitForResult());
}


TEST_CASE_METHOD(CodecTest, "Codec large uncompressible", "[blip][codec]") {
    init(GENERATE(false, true));
    roundTrip(ReadFile("/Library/Desktop Pictures/Abstract Shapes 2.jpg").waitForResult());
}

