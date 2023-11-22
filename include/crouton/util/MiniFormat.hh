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
 A string formatting API mostly compatible with `std::format`, but optimized for small code size.
 https://en.cppreference.com/w/cpp/utility/format/formatter
 */

namespace crouton::mini {
    using std::string;
    using std::string_view;

    
    class format_error : public std::runtime_error {
    public:
        explicit format_error(const char *msg) :runtime_error(msg) { }
    };


    namespace i {
        // Enumeration identifying all formattable types.
        enum class ArgType : uint8_t {
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

        struct ostreamableArg;

        using enum ArgType;

        // This maps types to FmtID values. Every formattable type needs an entry here.
        template <typename T> struct Formatting { };
        template<> struct Formatting<bool>              { static constexpr ArgType id = Bool; };
        template<> struct Formatting<char>              { static constexpr ArgType id = Char; };
        template<> struct Formatting<signed char>       { static constexpr ArgType id = Char; };
        template<> struct Formatting<unsigned char>     { static constexpr ArgType id = UInt; };
        template<> struct Formatting<short>             { static constexpr ArgType id = Int; };
        template<> struct Formatting<unsigned short>    { static constexpr ArgType id = UInt; };
        template<> struct Formatting<int>               { static constexpr ArgType id = Int; };
        template<> struct Formatting<unsigned int>      { static constexpr ArgType id = UInt; };
        template<> struct Formatting<long>              { static constexpr ArgType id = Long; };
        template<> struct Formatting<unsigned long>     { static constexpr ArgType id = ULong; };
        template<> struct Formatting<long long>         { static constexpr ArgType id = LongLong; };
        template<> struct Formatting<unsigned long long>{ static constexpr ArgType id = ULongLong; };
        template<> struct Formatting<float>             { static constexpr ArgType id = Double; };
        template<> struct Formatting<double>            { static constexpr ArgType id = Double; };
        template<> struct Formatting<const char*>       { static constexpr ArgType id = CString; };
        template<> struct Formatting<char*>             { static constexpr ArgType id = CString; };
        template<> struct Formatting<const void*>       { static constexpr ArgType id = Pointer; };
        template<> struct Formatting<void*>             { static constexpr ArgType id = Pointer; };
        template<> struct Formatting<std::string>       { static constexpr ArgType id = String; };
        template<> struct Formatting<std::string_view>  { static constexpr ArgType id = StringView; };
        template<> struct Formatting<ostreamableArg>               { static constexpr ArgType id = Arg; };
        template <ostreamable T> struct Formatting<T>   { static constexpr ArgType id = Arg; };
    }

    /** The concept `Formattable` defines what types can be passed as args to `format`. */
    template <typename T>
    concept Formattable = requires { i::Formatting<std::decay_t<T>>::id; };


    // ArgTypes<...>::ids is a C array of the ArgTypes corresponding to the template argument types.
    template<Formattable... Args>
    struct ArgTypes {
        static constexpr i::ArgType ids[] {i::Formatting<std::decay_t<Args>>::id... ,
                                           i::ArgType::None};
    };
    using ArgTypeList = i::ArgType const*;


    // A compiled format string
    class FormatString {
    public:
        consteval FormatString(const char* cstr) :_impl(parse(cstr)) { }

        string_view str() const     {return _impl._str;}

        enum class align_t : uint8_t {normal, left, center, right};
        enum class sign_t : uint8_t  {minusOnly, minusPlus, minusSpace};

        // Parsed format specifier
        struct Spec {
            char    fill            = ' ';
            uint8_t width           = 0;
            uint8_t precision       = 255;
            char    type            = 0;
            align_t align       :2  = align_t::normal;
            sign_t  sign        :2  = sign_t::minusOnly;
            bool    alternate   :1  = false;
            bool    zeroPad     :1  = false;
            bool    localize    :1  = false;
            bool    ordinary    :1  = true;     // if true, all other fields have default values

            constexpr void parse(const char*);
            friend constexpr bool operator==(Spec const& a, Spec const& b) = default;
        };


        // iterator over format string & specifiers
        class iterator {
        public:
            bool isLiteral() const              {return _str[0] != '{' || _str[1] == '{';}
            struct Spec const& spec() const     {return *_pSpec;}
            string_view literal() const;
            iterator& operator++ ();
            friend bool operator== (iterator const& a, iterator const& b) {
                return a._pLength == b._pLength;}
        private:
            friend class FormatString;

            iterator(FormatString const& fmt);
            explicit iterator(uint8_t const* endLength) :_pLength(endLength) { }

            const char*     _str;
            uint8_t const*  _pLength;
            Spec const*     _pSpec;
        };

        iterator begin() const Pure {return iterator(*this);}
        iterator end()   const Pure {return iterator(&_impl._lengths[_impl._nSegments]);}

    private:
        static constexpr size_t kMaxSegments = 15;
        static constexpr size_t kMaxSpecs = 8;

        struct Impl {
            const char* const   _str;
            uint8_t             _nSegments;
            uint8_t             _lengths[kMaxSegments];
            Spec                _specs[kMaxSpecs];
        };

        static constexpr Impl parse(const char* cstr);

        Impl const _impl;
    };


    namespace i {
        // Struct that type-erases an `ostreamable` value; passed as arg to formatting fns.
        struct ostreamableArg {
            template <ostreamable T>
            static ostreamableArg make(T &&value) {
                return ostreamableArg{._ptr = reinterpret_cast<const void*>(&value),
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
        inline auto passArg(ostreamableArg const& t)        {return t;}

        template <ostreamable T>
        requires (i::Formatting<T>::id == ArgType::Arg)
        auto passArg(T const& t)                            {return ostreamableArg::make(t);}

        static constexpr bool isdigit(char c) {
            return c >= '0' && c <= '9';
        }
        static constexpr bool isalpha(char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }
        static constexpr int digittoint(char c) {
            return c - '0';
        }
    }


    // The core formatting functions:
    void format_types(ostream&, FormatString const& fmt, ArgTypeList types, ...);
    void vformat_types(ostream&, FormatString const& fmt, ArgTypeList types, va_list);
    string format_types(FormatString const&, ArgTypeList types, ...);
    string vformat_types(FormatString const&, ArgTypeList types, va_list);


    /** Writes formatted output to an ostream.
        @param out  The stream to write to.
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    void format(ostream& out, FormatString const& fmt, Args &&...args) {
        format_types(out, fmt, ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Returns a formatted string..
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    string format(FormatString const& fmt, Args &&...args) {
        return format_types(fmt, ArgTypes<Args...>::ids, i::passArg(args)...);
    }


#pragma mark - Method implementations:

    // (These have to be in the header because they're constexpr.)

    constexpr FormatString::Impl FormatString::parse(const char* cstr) {
        Impl impl {cstr, 0, {}, {}};
        unsigned iSegment = 0, iSpec = 0;
        size_t lastPos = 0;

        auto append = [&](size_t pos) {
            if(iSegment >= kMaxSegments)
                throw format_error("Too many format specifiers");
            if (pos - lastPos > 0xFF)
                throw format_error("Format string too long");
            impl._lengths[iSegment++] = uint8_t(pos - lastPos);
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
                    throw format_error("Invalid '}' in format string");
                pos += 2;
            } else if (pos + 1 < str.size() && str[pos + 1] == '{') {
                // "{{" is an escape
                pos += 2;
            } else {
                auto endPos = str.find('}', pos + 1);
                if (endPos == string::npos)
                    throw format_error("Unclosed format specifier");
                if (iSpec >= kMaxSpecs)
                    throw format_error("Too many format specifiers");
                impl._specs[iSpec++].parse(&str[pos + 1]);
                pos = endPos + 1;
            }
            append(pos);
        }

        pos = str.size();
        if (pos > lastPos)
            append(pos);
        impl._nSegments = uint8_t(iSegment);
        return impl;
    }

    constexpr void FormatString::Spec::parse(const char* str) {
        // https://en.cppreference.com/w/cpp/utility/format/formatter

        // skip arg-id for now
        for (; *str != ':'; ++str) {
            if (*str == '}') return; // empty spec `{}`
            else if (!i::isdigit(*str))
                throw format_error("invalid format spec: invalid arg number "
                                            "(did you forget the ':'?)");
            throw format_error("invalid format spec: arg numbers not supported "
                                        "(or did you forget the ':'?)");
        }
        ++str;
        if (str[0] == '}') return; // empty spec `{:}`

        this->ordinary = false;

        // parse fill and align:
        char alignChar = 0;
        if (str[0] == '<' || str[0] == '^' || str[0] == '>') {
            this->fill = ' ';
            alignChar = str[0];
            str += 1;
        } else if (str[1] == '<' || str[1] == '^' || str[1] == '>') {
            this->fill = str[0];
            alignChar = str[1];
            str += 2;
        }
        switch (alignChar) {
            case '<':   this->align = align_t::left; break;
            case '^':   this->align = align_t::center; break;
            case '>':   this->align = align_t::right; break;
        }

        // parse sign:
        switch (*str++) {
            case '}': return;
            case '-': this->sign = sign_t::minusOnly;  break;
            case '+': this->sign = sign_t::minusPlus;  break;
            case ' ': this->sign = sign_t::minusSpace; break;
            default:  --str; break;
        }

        // parse '#' and '0':
        if (*str == '#') {
            this->alternate = true;
            ++str;
        }
        if (*str == '0' && alignChar == 0) {
            this->fill = '0';
            this->align = align_t::right;
            ++str;
        }

        // parse width:
        if (i::isdigit(*str)) {
            unsigned w = 0;
            while (i::isdigit(*str)) {
                w = 10 * w + i::digittoint(*str++);
                if (w > 255)
                    throw format_error("invalid format spec: width too large");
            }
            this->width = uint8_t(w);
        }
        // parse precision:
        if (*str == '.') {
            if (!i::isdigit(*++str))
                throw format_error("invalid format spec: invalid precision");
            unsigned p = 0;
            while (i::isdigit(*str)) {
                p = 10 * p + i::digittoint(*str++);
                if (p > 255)
                    throw format_error("invalid format spec: precision too large");
            }
            this->precision = uint8_t(p);
        }
        // "localized" specifier:
        if (*str == 'L') {
            this->localize = true;
            ++str;
        }
        // type char:
        if (*str != '}') {
            if (!i::isalpha(*str) && *str != '?')
                throw format_error("invalid format spec: unknown type");
            this->type = *str;
            if (str[1] != '}')
                throw format_error("invalid format spec: unknown chars at end");
        }
    }

}
