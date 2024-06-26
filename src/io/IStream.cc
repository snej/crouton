//
// IStream.cc
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

#include "crouton/io/IStream.hh"
#include "crouton/Future.hh"
#include "crouton/util/Bytes.hh"
#include <cstring> // for memcpy

namespace crouton::io {
    using namespace std;


    void closeThenRelease(std::shared_ptr<IStream> &&stream) {
        // Final release is deferred until the end of the 'then' callback.
        (void) stream->close().then([=]() mutable {stream.reset();});
    }

    
    Future<size_t> IStream::read(MutableBytes buf) {
        size_t bytesRead = 0;
        while (bytesRead < buf.size()) {
            ConstBytes bytes = AWAIT readNoCopy(buf.size() - bytesRead);
            if (bytes.size() == 0)
                break;
            ::memcpy(buf.data() + bytesRead, bytes.data(), bytes.size());
            bytesRead += bytes.size();
        }
        RETURN bytesRead;
    }


    Future<void> IStream::readExactly(MutableBytes buf) {
        return read(buf).then([buf](size_t bytesRead) {
            if (bytesRead < buf.size())
                Error(CroutonError::UnexpectedEOF).raise();
        });
    }


    Future<string> IStream::readString(size_t maxLen) {
        static constexpr size_t kGrowSize = 32768;
        string data;
        size_t len = 0;
        while (len < maxLen) {
            size_t n = std::min(kGrowSize, maxLen - len);
            data.resize(len + n);
            size_t bytesRead = AWAIT read({&data[len], n});

            if (bytesRead < n) {
                data.resize(len + bytesRead);
                break;
            }
            len += bytesRead;
        }
        RETURN data;
    }


    Future<string> IStream::readUntil(string end, size_t maxLen) {
        precondition(!end.empty());
        precondition(maxLen >= end.size());
        string data;
        while (data.size() < maxLen) {
            auto dataLen = data.size();
            ConstBytes peek = AWAIT peekNoCopy();
            if (peek.size() > maxLen - dataLen)
                peek = peek.first(maxLen - dataLen);

            // Check for a match that's split between the old and new data:
            if (!data.empty()) {
                data.append((char*)peek.data(), min(end.size() - 1, peek.size()));
                size_t startingAt = dataLen - std::min(end.size(), dataLen);
                if (auto found = data.find(end, startingAt); found != string::npos) {
                    // Found it:
                    found += end.size();
                    found = std::min(found, maxLen);
                    data.resize(found);
                    (void) AWAIT readNoCopy(found - dataLen);  // consume the bytes I used
                    break;
                } else {
                    data.resize(dataLen);
                }
            }

            // Check for a match in the new data:
            if (auto found = string_view((char*)peek.data(), peek.size()).find(end); found != string::npos) {
                found += end.size();
                found = std::min(found, maxLen - data.size());
                data.append((char*)peek.data(), found);
                (void) AWAIT readNoCopy(found);  // consume the bytes I used
                break;
            }

            // Otherwise append all the input data and read more:
            size_t addLen = std::min(peek.size(), maxLen - data.size());
            data.append((char*)peek.data(), addLen);
            (void) AWAIT readNoCopy(addLen);
        }
        RETURN data;
    }


    ASYNC<string> IStream::readAll() {return readString(SIZE_MAX);}


    Generator<string> IStream::generate() {
        if (!isOpen())
            AWAIT open();
        while (true) {
            ConstBytes bytes = AWAIT readNoCopy();
            if (bytes.size() == 0)
                break;
            YIELD string(string_view(bytes));
        }
    }


    Future<void> IStream::write(string str) {
        // Use co_await to ensure `str` stays in scope until the write completes.
        AWAIT write(ConstBytes(str));
        RETURN noerror;
    }


    Future<void> IStream::write(const ConstBytes buffers[], size_t nBuffers) {
        if (nBuffers == 1) {
            AWAIT write(buffers[0]);
        } else if (nBuffers > 1) {
            // After the first AWAIT the caller's stack frame will be gone,
            // and also the buffers[] C array. So copy it into a vector:
            vector<ConstBytes> buffersCopy(&buffers[0], &buffers[nBuffers]);
            for (auto& buffer : buffersCopy)
                AWAIT write(buffer);
        }
        RETURN noerror;
    }


    Future<void> IStream::write(std::initializer_list<ConstBytes> buffers) {
        return write(buffers.begin(), buffers.size());
    }


}
