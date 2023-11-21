//
// MiniFormat.hh
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
#include "MiniOStream.hh"
#include <concepts>
#include <cstdarg>
#include <initializer_list>
#include <string>
#include <string_view>

/*
 A string formatting API somewhat compatible with `std::format`, but optimized for small code size.
 */

namespace crouton::mini {
    using std::string;
    using std::string_view;

    namespace i {
        // Enumeration identifying all formattable types.
        enum class FmtID : uint8_t {
            None = 0,
            Bool,
            Char,
            Int,
            UInt,
            Long,
            ULong,
            LongLong,
            ULongLong,
            Double,
            CString,
            Pointer,
            String,
            StringView,
            Arg,
        };

        struct arg;

        using enum FmtID;

        // This maps types to FmtID values. Every formattable type needs an entry here.
        template <typename T> struct Formatting { };
        template<> struct Formatting<bool>              { static constexpr FmtID id = Bool; };
        template<> struct Formatting<char>              { static constexpr FmtID id = Char; };
        template<> struct Formatting<signed char>       { static constexpr FmtID id = Char; };
        template<> struct Formatting<unsigned char>     { static constexpr FmtID id = UInt; };
        template<> struct Formatting<short>             { static constexpr FmtID id = Int; };
        template<> struct Formatting<unsigned short>    { static constexpr FmtID id = UInt; };
        template<> struct Formatting<int>               { static constexpr FmtID id = Int; };
        template<> struct Formatting<unsigned int>      { static constexpr FmtID id = UInt; };
        template<> struct Formatting<long>              { static constexpr FmtID id = Long; };
        template<> struct Formatting<unsigned long>     { static constexpr FmtID id = ULong; };
        template<> struct Formatting<long long>         { static constexpr FmtID id = LongLong; };
        template<> struct Formatting<unsigned long long>{ static constexpr FmtID id = ULongLong; };
        template<> struct Formatting<float>             { static constexpr FmtID id = Double; };
        template<> struct Formatting<double>            { static constexpr FmtID id = Double; };
        template<> struct Formatting<const char*>       { static constexpr FmtID id = CString; };
        template<> struct Formatting<char*>             { static constexpr FmtID id = CString; };
        template<> struct Formatting<const void*>       { static constexpr FmtID id = Pointer; };
        template<> struct Formatting<void*>             { static constexpr FmtID id = Pointer; };
        template<> struct Formatting<std::string>       { static constexpr FmtID id = String; };
        template<> struct Formatting<std::string_view>  { static constexpr FmtID id = StringView; };
        template<> struct Formatting<arg>               { static constexpr FmtID id = Arg; };
        template <ostreamable T> struct Formatting<T>   { static constexpr FmtID id = Arg; };

        // Returns the FmtID value corresponding to the type of its argument.
        template <typename T>
        consteval FmtID getFmtID(T&&) { return Formatting<std::decay_t<T>>::id; }
    }

    /** The concept `Formattable` defines what types can be passed as args to `format`. */
    template <typename T>
    concept Formattable = requires { i::Formatting<std::decay_t<T>>::id; };


    // FmtIDs<...>::ids is a C array of the FmtIDs corresponding to the template argument types.
    template<Formattable... Args>
    struct FmtIDs {
        static constexpr i::FmtID ids[] {i::Formatting<std::decay_t<Args>>::id... , i::FmtID::None};
    };
    using FmtIDList = i::FmtID const*;


    class FormatString {
    public:
        consteval FormatString(const char* cstr) :_spec(parse(cstr)) { }

        size_t size() const Pure {return _spec._size;}

        string_view operator[] (size_t part) const Pure {
            assert(part < _spec._size);
            const char* start = _spec._str;
            for (size_t i = 0; i < part; i++)
                start += _spec._parts[i];
            return string_view(start, _spec._parts[part]);
        }

    private:
        struct spec {
            const char* const   _str;
            uint8_t             _size;
            uint8_t             _parts[15];
        };

        static constexpr spec parse(const char* cstr) {
            spec s {cstr, 0, {}};
            unsigned part = 0;
            size_t lastPos = 0;

            auto append = [&](size_t pos) {
                if(part >= sizeof(s._parts))
                    throw std::invalid_argument("Too many format specifiers");
                if (pos - lastPos > 0xFF)
                    throw std::invalid_argument("Format string too long");
                s._parts[part++] = uint8_t(pos - lastPos);
                lastPos = pos;
            };

            string_view str(cstr);
            size_t pos;
            while (string::npos != (pos = str.find_first_of("{}", lastPos))) {
                if (pos > lastPos)
                    append(pos);
                if (str[pos] == '}') {
                    // (The only reason to pay attention to "}" is that the std::format spec says
                    // "}}" is an escape and should be emitted as "}". Otherwise a "} is a syntax
                    // error, but let's just emit it as-is.
                    if (pos + 1 >= str.size() || str[pos + 1] != '}')
                        throw std::invalid_argument("Invalid '}' in format string");
                    pos += 2;
                } else if (pos + 1 < str.size() && str[pos + 1] == '{') {
                    // "{{" is an escape
                    pos += 2;
                } else {
                    pos = str.find('}', pos + 1);
                    if (pos == string::npos)
                        throw std::invalid_argument("Unclosed format specifier");
                    ++pos;
                }
                append(pos);
            }
            
            pos = str.size();
            if (pos > lastPos)
                append(pos);
            s._size = uint8_t(part);
            return s;
        }

        spec const _spec;
    };


    namespace i {
        // Struct that type-erases an `ostreamable` value; passed as arg to formatting fns.
        struct arg {
            template <ostreamable T>
            static arg make(T &&value) {
                return arg{._ptr = reinterpret_cast<const void*>(&value),
                           ._write = &writeFn<std::remove_cvref_t<T>>};
            }

            void writeTo(ostream& out) const                        {_write(out, _ptr);}

            using writeFn_t = void (*)(ostream&, const void*);

            template <typename T>
            static void writeFn(ostream& out, const void* ptr)      { out << *(const T*)ptr; }

            const void* _ptr;                       // address of value -- type-erased T*
            writeFn_t   _write;                     // pointer to writeFn<T>()
        };

        // Transforms args before they're passed as varargs to `format`.
        template <std::integral T>       auto passArg(T t)  {return t;}
        template <std::floating_point T> auto passArg(T t)  {return t;}
        inline auto passArg(char* t)                        {return t;}
        inline auto passArg(const char* t)                  {return t;}
        inline auto passArg(void* t)                        {return t;}
        inline auto passArg(const void* t)                  {return t;}
        inline auto passArg(std::string const& t)           {return &t;}
        inline auto passArg(std::string_view const& t)      {return &t;}
        inline auto passArg(arg const& t)                   {return t;}

        template <ostreamable T>
        requires (i::Formatting<T>::id == FmtID::Arg)
        auto passArg(T const& t)                            {return arg::make(t);}
    }


    // The core formatting functions:
    void format_types(ostream&, FormatString const& fmt, FmtIDList types, ...);
    void vformat_types(ostream&, FormatString const& fmt, FmtIDList types, va_list);
    string format_types(FormatString const&, FmtIDList types, ...);
    string vformat_types(FormatString const&, FmtIDList types, va_list);


    /** Writes formatted output to an ostream.
        @param out  The stream to write to.
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    void format(ostream& out, FormatString const& fmt, Args &&...args) {
        format_types(out, fmt, FmtIDs<Args...>::ids, i::passArg(args)...);
    }


    /** Returns a formatted string..
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    string format(FormatString const& fmt, Args &&...args) {
        return format_types(fmt, FmtIDs<Args...>::ids, i::passArg(args)...);
    }
}
