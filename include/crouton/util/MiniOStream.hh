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
#include "crouton/util/Bytes.hh"
#include <concepts>

/*
 Lightweight replacement for std::ostream.
 */

namespace crouton::mini {

    static constexpr const char* endl = "\n";

    /** Abstract base class of output streams. */
    class ostream {
    public:
        virtual ~ostream() = default;
        virtual ostream& write(const char* src, size_t len) =0;

        ostream& write(const char* begin, const char* end) {return write(begin, end - begin);}

        ostream& write(ConstBytes b);
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
        stringstream(std::string s)     :_str(std::move(s)) { }

        ostream& write(const char* src, size_t len) override {_str.append(src, len); return *this;}

        string const& str() const &     {return _str;}
        string&& str() &&               {return std::move(_str);}
        string extract_str()            {return std::move(_str);}

        template <typename T>
        void str(T&& s)                 {_str = std::forward<T>(s);}

        void clear()                    {_str.clear();}

    private:
        std::string _str;
    };


    /** ostream that writes to a fixed-size caller-provided buffer. */
    class bufferstream final : public ostream {
    public:
        bufferstream(char* begin, char* end)
            :_begin(begin), _next(begin), _end(end) {assert(_end >= _begin); }
        explicit bufferstream(MutableBytes b) :bufferstream((char*)b.data(), (char*)b.endByte()) { }

        ostream& write(const char* src, size_t len) override {
            if (_next + len > _end)
                throw std::runtime_error("bufferstream overflow");
            ::memcpy(_next, src, len);
            _next += len;
            return *this;
        }

        size_t available() const Pure       {return _end - _next;}

        string_view str() const Pure        {return {_begin, _next};}
        MutableBytes bytes() const Pure     {return {_begin, _next};}
        MutableBytes buffer() const Pure    {return {_begin, _end};}

        void clear()                        {_next = _begin;}

    private:
        char *_begin, *_next, *_end;
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


    inline ostream& operator<< (ostream& o, ConstBytes bytes)    {return o.write(bytes);}
    inline ostream& operator<< (ostream& o, const char* str)     {return o.write(str);}
    inline ostream& operator<< (ostream& o, string_view str)     {return o.write(str);}
    inline ostream& operator<< (ostream& o, string const& str)   {return o.write(str);}
    inline ostream& operator<< (ostream& o, char c)              {return o.write(&c, 1);}

    ostream& operator<< (ostream&, const void*);

    template <std::signed_integral INT>
    ostream& operator<< (ostream& o, INT i)             {return o.writeInt64(int64_t(i));}

    template <std::unsigned_integral UINT>
    ostream& operator<< (ostream& o, UINT i)            {return o.writeInt64(uint64_t(i));}

    inline ostream& operator<< (ostream& o, float f)    {return o.writeDouble(f);}
    inline ostream& operator<< (ostream& o, double d)   {return o.writeDouble(d);}

    
    /** The concept `ostreamable` defines types that can be written to an ostream with `<<`. */
    template <typename T>
    concept ostreamable = requires(ostream& out, T t) { out << t; };
}
