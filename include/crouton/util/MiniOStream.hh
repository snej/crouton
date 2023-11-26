//
// MiniOStream.hh
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

#pragma once
#include <concepts>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#if __has_include("betterassert.hh")
#include "betterassert.hh"
#else
#include <cassert>
#endif

/*
 Lightweight replacement for std::ostream.
 */

namespace crouton::mini {
    using string = std::string;
    using string_view = std::string_view;

#ifdef _WIN32
    static constexpr const char* endl = "\n\r";
#else
    static constexpr const char* endl = "\n";
#endif

    /** Abstract base class of output streams. */
    class ostream {
    public:
        virtual ~ostream() = default;
        virtual ostream& write(const char* src, size_t len) =0;

        ostream& write(const char* begin, const char* end) {return write(begin, end - begin);}

        ostream& write(std::span<const char> b);
        ostream& write(std::span<const std::byte> b);
        ostream& write(const char* str);
        ostream& write(string const& str);

        ostream& writeInt64(int64_t, int base =10);
        ostream& writeUInt64(uint64_t, int base =10);
        ostream& writeDouble(double);

        virtual void flush() { }

    protected:
        ostream() = default;
        ostream(ostream const&) = delete;
        ostream& operator=(ostream const&) = delete;
    };


    /** ostream that writes to a string. */
    class stringstream final : public ostream {
    public:
        stringstream() = default;
        stringstream(string s)     :_str(std::move(s)) { }

        ostream& write(const char* src, size_t len) override {_str.append(src, len); return *this;}

        string const& str() const &      {return _str;}
        string str() &&                  {return std::move(_str);}

        template <typename T>
        void str(T&& s)                 {_str = std::forward<T>(s);}

        string_view view() const        {return _str;}

        void clear()                    {_str.clear();}

    private:
        string _str;
    };


    /** ostream that writes to a fixed-size caller-provided buffer. */
    class bufstream : public ostream {
    public:
        bufstream(char* begin, char* end)
        :_begin(begin), _next(begin), _end(end) {assert(_end >= _begin); }
        bufstream(char* begin, size_t size)     :bufstream(begin, begin + size) { }
        explicit bufstream(std::span<char> b)   :bufstream(b.data(), b.size_bytes()) { }

        ostream& write(const char* src, size_t len) override {
            if (_next + len > _end)
                throw std::runtime_error("bufferstream overflow");
            ::memcpy(_next, src, len);
            _next += len;
            return *this;
        }

        size_t available() const            {return _end - _next;}

        string_view str() const             {return {_begin, _next};}
        std::span<char> buffer() const      {return {_begin, _end};}

        void clear()                        {_next = _begin;}

    protected:
        bufstream() = default;
        char *_begin, *_next, *_end;
    };


    /** ostream that writes to a fixed-size buffer it allocates itself.
        A small buffer lives inside the object; a large one is heap-allocated. */
    template <size_t SIZE>
    class owned_bufstream : public bufstream {
    public:
        owned_bufstream()  :bufstream(SIZE > kMaxInlSize ? new char[SIZE] : _buffer, SIZE) { }
        ~owned_bufstream() {if constexpr (SIZE > kMaxInlSize) delete[] _begin;}
    private:
        static constexpr size_t kMaxInlSize = 64;
        char _buffer[ (SIZE <= kMaxInlSize) ? SIZE : 1 ];
    };


    /** Minimalist file stream suitable for cout and cerr. */
    class fdstream final : public ostream {
    public:
        explicit fdstream(FILE* f)  :_fd(f) { }
        ostream& write(const char* src, size_t len) override;
        void flush() override;
    private:
        FILE* _fd;
    };

    /** ostream that writes to stdout. */
    extern fdstream cout;
    
    /** ostream that writes to stderr. */
    extern fdstream cerr;


    inline ostream& operator<< (ostream& o, std::span<const std::byte> bytes)    {return o.write(bytes);}
    inline ostream& operator<< (ostream& o, const char* str)     {return o.write(str);}
    inline ostream& operator<< (ostream& o, string_view str)     {return o.write(str.data(), str.size());}
    inline ostream& operator<< (ostream& o, string const& str)   {return o.write(str);}
    inline ostream& operator<< (ostream& o, char c)              {return o.write(&c, 1);}

    ostream& operator<< (ostream&, const void*);

    template <std::signed_integral INT>
    ostream& operator<< (ostream& o, INT i)             {return o.writeInt64(int64_t(i));}

    template <std::unsigned_integral UINT>
    ostream& operator<< (ostream& o, UINT i)            {return o.writeUInt64(uint64_t(i));}

    inline ostream& operator<< (ostream& o, float f)    {return o.writeDouble(f);}
    inline ostream& operator<< (ostream& o, double d)   {return o.writeDouble(d);}

    
    /** The concept `ostreamable` defines types that can be written to an ostream with `<<`. */
    template <typename T>
    concept ostreamable = requires(ostream& out, T t) { out << t; };
}
