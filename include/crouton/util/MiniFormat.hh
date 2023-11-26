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
#include <stdexcept>
#include <string>
#include <string_view>

/*
 A string formatting API mostly compatible with `std::format`, but optimized for small code size.
 https://en.cppreference.com/w/cpp/utility/format/formatter

 Missing functionality and limitations:
 - You can't create custom formatters that interpret custom field specs. (But you can format
   custom types by implementing `operator<<`.)
 - Arguments can't be reordered (i.e. a field spec like `{nn:}` isn't allowed.)
 - Field widths & alignment are not Unicode-aware; they assume 1 byte == 1 space.
 - Localized variants are unimplemented (using 'L' in a format spec has no effect.)
 - Only 10 arguments are allowed. (You can change this by changing BaseFormatString::kMaxSpecs.)
 - Field width and precision are limited to 255.

 Known bugs:
 - When a number is zero-padded, the zeroes go before the sign character, not afterward.
 - When the alternate ('#') form of a float adds a decimal point, it will go after any exponent,
   when it should go before.
 */

namespace crouton::mini {
    using std::string;
    using std::string_view;


#pragma mark - ARGUMENT TYPES:


    namespace i { // internal stuff
        // Enumeration identifying the argument types passed thru varargs.
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
            String,
            StringView,
            Pointer,
            Arg,
        };

        static constexpr const char kDefaultTypeCharForArgType[] = " scdddddd sssp ";
        static_assert(sizeof(kDefaultTypeCharForArgType) == size_t(ArgType::Arg) + 2);

        static constexpr const char* kValidTypeCharsForArgType[15] = {
            "",
            "sbBdoxX",      // Bool
            "cbBdoxX",      // Char
            "bBcdoxX", "bBcdoxX", "bBcdoxX", "bBcdoxX", "bBcdoxX", "bBcdoxX",  // integers
            "aAeEfFgG",     // Double
            "s", "s", "s"   // strings
            "pP",           // Pointer
            "s"             // Arg
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
        template<> struct Formatting<unsigned long long>{ static constexpr ArgType id = ULongLong;};
        template<> struct Formatting<float>             { static constexpr ArgType id = Double; };
        template<> struct Formatting<double>            { static constexpr ArgType id = Double; };
        template<> struct Formatting<const char*>       { static constexpr ArgType id = CString; };
        template<> struct Formatting<char*>             { static constexpr ArgType id = CString; };
        template<> struct Formatting<const void*>       { static constexpr ArgType id = Pointer; };
        template<> struct Formatting<void*>             { static constexpr ArgType id = Pointer; };
        template<> struct Formatting<std::string>       { static constexpr ArgType id = String; };
        template<> struct Formatting<std::string_view>  { static constexpr ArgType id= StringView;};
        template<> struct Formatting<ostreamableArg>    { static constexpr ArgType id = Arg; };
        template <ostreamable T> struct Formatting<T>   { static constexpr ArgType id = Arg; };
    }


    /** The concept `Formattable` matches the types that can be passed as args to `format`. */
    template <typename T>
    concept Formattable = requires { i::Formatting<std::decay_t<T>>::id; };


    /** A pointer to a C array of argument types, terminated with `None`.
        This is constructed at compile-time and passed to the `format` implementation. */
    using ArgTypeList = i::ArgType const*;


    // `ArgTypes<...>::ids` is an `ArgTypeList` whose items match the template arguments.
    template<Formattable... Args>
    struct ArgTypes {
        static constexpr i::ArgType ids[] {i::Formatting<std::decay_t<Args>>::id... ,
                                           i::ArgType::None};
    };


    namespace i { // internal stuff, cont'd
        // Struct that type-erases an `ostreamable` value; passed as arg to formatting fns.
        // Since this is passed through varargs it cannot have a constructor.
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

            const void* _ptr;                       // address of value -- a type-erased `T*`
            writeFn_t   _write;                     // address of `writeFn<T>()`
        };

        // `passArg()` transforms args before they're passed as varargs to `format`.
        template <std::integral T>       auto passArg(T t)  {return t;}
        template <std::floating_point T> auto passArg(T t)  {return t;}
        inline auto passArg(char* t)                        {return t;}
        inline auto passArg(const char* t)                  {return t;}
        inline auto passArg(void* t)                        {return t;}
        inline auto passArg(const void* t)                  {return t;}
        inline auto passArg(std::string const& t)           {return &t;} // pass by reference!
        inline auto passArg(std::string_view const& t)      {return &t;} // pass by reference!

        template <ostreamable T> // ostreamable values are passed as a type-erased struct
        requires (i::Formatting<T>::id == ArgType::Arg)
        auto passArg(T const& t)                            {return ostreamableArg::make(t);}

        // constexpr and non-localized versions of ctypes fns
        static constexpr bool isdigit(char c)   {return c >= '0' && c <= '9';}
        static constexpr bool isalpha(char c)   {return (c >= 'A' && c <= 'Z') ||
                                                        (c >= 'a' && c <= 'z');}
        static constexpr int digittoint(char c) {return c - '0';}
    }


#pragma mark - FORMAT STRING:


    /** A compiled format string. Functions that are passed format strings by other functions
        instead of being called directly should take a `BaseFormatString const&` parameter; see
        `format_types()` and `vformat_types` as examples. */
    class BaseFormatString {
    public:
        consteval BaseFormatString(const char* cstr, ArgTypeList argTypes)
        :_impl(parse(cstr, argTypes)) { }

        string_view get() const     {return _impl._str;}

        enum class align_t : uint8_t {left, center, right};
        enum class sign_t  : uint8_t {minusOnly, minusPlus, minusSpace};
        static constexpr uint8_t kDefaultPrecision = 255, kMaxPrecision = 255, kMaxWidth = 255;

        // Parsed format specifier
        struct Spec {
            char    type            = 0;
            char    fill            = ' ';
            uint8_t width           = 0;
            uint8_t precision       = kDefaultPrecision;
            align_t align       :2  = align_t::left;
            sign_t  sign        :2  = sign_t::minusOnly;
            bool    alternate   :1  = false;
            bool    localize    :1  = false;

            constexpr void parse(const char*, i::ArgType);
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
            friend class BaseFormatString;
            iterator(BaseFormatString const& fmt);
            explicit iterator(uint8_t const* endLength) :_pLength(endLength) { }

            const char*     _str;
            uint8_t const*  _pLength;
            Spec const*     _pSpec;
        };

        iterator begin() const {return iterator(*this);}
        iterator end()   const {return iterator(&_impl._lengths[_impl._nSegments]);}

#ifndef NDEBUG
        static BaseFormatString testParse(const char* cstr, ArgTypeList argTypes) {
            return BaseFormatString(parse(cstr, argTypes));
        }
#endif
    private:
        static constexpr size_t kMaxSpecs = 10;
        static constexpr size_t kMaxSegments = 2 * kMaxSpecs + 1;

        struct Impl {
            const char* const   _str;                   // the format string
            uint8_t             _nSegments;             // number of pieces; size of _lengths[]
            uint8_t             _lengths[kMaxSegments]; // length in bytes of each piece of _str
            Spec                _specs[kMaxSpecs];      // format specs, in order
        };

        explicit BaseFormatString(Impl impl) :_impl(impl) { }
        static constexpr Impl parse(const char* cstr, ArgTypeList);

        Impl const _impl;
    };


    // Templated subclass of BaseFormatString that takes the arg types as format parameters,
    // so that the ArgTypeList can be passed to the parent constructor for compile-time checking.
    // (The `type_identity` thing is magic; I just copied it from the `std::format_string` docs:
    // https://en.cppreference.com/w/cpp/utility/format/basic_format_string )
    template<Formattable... Args>
    class FormatString_ : public BaseFormatString {
    public:
        consteval FormatString_(const char* cstr) 
        :BaseFormatString(cstr, ArgTypes<Args...>::ids) { }
    };

    /** A format string, templated by the types of the runtime arguments.
        Client-visible formatting functions should take a (reference to) this type,
        using the parameter pack as the template args. See `format()` for example. */
    template<Formattable... Args>
    using FormatString = FormatString_<std::type_identity_t<std::decay_t<Args>>...>;


    // The core formatting functions implemented in the .cc file
    void format_types_to(ostream&, BaseFormatString const& fmt, ArgTypeList types, ...);
    void vformat_types_to(ostream&, BaseFormatString const& fmt, ArgTypeList types, va_list);
    string format_types(BaseFormatString const&, ArgTypeList types, ...);
    string vformat_types(BaseFormatString const&, ArgTypeList types, va_list);


    /** Configuration parameter: If this is `true`, the `format` functions allow you to pass more
        arguments than are specified in the format string. The extra arguments are printed after
        a ":", with default formatting, and separated by ","s. */
    static constexpr bool kAllowExtraArgs = true;


    /** Writes formatted output to a `mini::ostream`.
        @param out  The stream to write to.
        @param fmt  Format string. Must be a string literal, with `{…}` placeholders for args.
        @param args  Arguments, of any types satisfying `Formattable`. */
    template<Formattable... Args>
    inline void format_to(ostream& out, FormatString<Args...> const& fmt, Args &&...args) {
        format_types_to(out, fmt, ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Returns a formatted string. This is mostly a drop-in replacement for `std::format()`.
        @param fmt  Format string. Must be a string literal, with `{…}` placeholders for args.
        @param args  Arguments, of any types satisfying `Formattable`. */
    template<Formattable... Args>
    inline string format(FormatString<Args...> const& fmt, Args &&...args) {
        return format_types(fmt, ArgTypes<Args...>::ids, i::passArg(args)...);
    }


    /** Exception thrown for invalid format specs or invalid arguments.
        Mostly gets thrown _at compile time_, which manifests as a confusing build error about
        a `consteval` function calling a runtime-only function (i.e. `throw`.) */
    class format_error : public std::runtime_error {
    public:
        explicit format_error(const char *msg) :runtime_error(msg) { }
    };


#pragma mark - Method implementations:

    // (These have to be in the header because they're constexpr and run at compile time.)

    constexpr BaseFormatString::Impl BaseFormatString::parse(const char* cstr,
                                                             ArgTypeList argTypes)
    {
        Impl impl {cstr, 0, {}, {}};
        unsigned nSpecs = 0;
        auto iArg = argTypes;

        // subroutine to append a string segment to `_lengths`
        size_t lastPos = 0;
        auto addSegment = [&](size_t pos) {
            if (pos > lastPos) {
                if (impl._nSegments >= kMaxSegments)
                    throw format_error("Too many format specifiers");
                if (pos - lastPos > 0xFF)
                    throw format_error("Format string too long");
                impl._lengths[impl._nSegments++] = uint8_t(pos - lastPos);
                lastPos = pos;
            }
        };

        string_view str(cstr);
        size_t pos;
        while (string::npos != (pos = str.find_first_of("{}", lastPos))) {
            addSegment(pos);
            if (str[pos] == '}') {
                // "}}" is an escape and should be emitted as "}". Otherwise a "} is a syntax error:
                if (pos + 1 >= str.size() || str[pos + 1] != '}')
                    throw format_error("Invalid '}' in format string");
                pos += 2;
            } else if (pos + 1 < str.size() && str[pos + 1] == '{') {
                // "{{" is an escape, emitted as "{":
                pos += 2;
            } else {
                // OK, we have a format specifier!
                auto endPos = str.find('}', pos + 1);
                if (endPos == string::npos)
                    throw format_error("Unclosed format specifier");
                else if (nSpecs >= kMaxSpecs)
                    throw format_error("Too many format specifiers");
                else if (*iArg == i::ArgType::None)
                    throw format_error("More format specifiers than arguments");
                impl._specs[nSpecs++].parse(&str[pos + 1], *iArg++);
                pos = endPos + 1;
            }
            addSegment(pos);
        }

        // Add the last literal segment, if non-empty:
        addSegment(str.size());
        return impl;
    }


    constexpr void BaseFormatString::Spec::parse(const char* str, i::ArgType argType) {
        // https://en.cppreference.com/w/cpp/utility/format/formatter
        // Set a default type based on the arg type:
        if (char c = i::kDefaultTypeCharForArgType[uint8_t(argType)]; c != ' ')
            this->type = c;
        // Numbers default to right alignment:
        if (argType >= i::ArgType::Int && argType <= i::ArgType::Double)
            this->align = BaseFormatString::align_t::right;

        // parse argument number, if any:
        for (; *str != ':'; ++str) {
            if (*str == '}') return; // -> empty spec `{}`
            else if (!i::isdigit(*str))
                throw format_error("invalid format spec: invalid arg number "
                                   "(did you forget the ':'?)");
            throw format_error("invalid format spec: arg numbers not supported "
                               "(or did you forget the ':'?)");  //TODO: Implement arg numbers
        }
        ++str;
        if (str[0] == '}') return; // -> empty spec `{:}`

        // parse fill and align:
        char alignChar = 0;
        if (char c = str[0]; c == '<' || c == '^' || c == '>') {
            this->fill = ' ';
            alignChar = c;
            str += 1;
        } else if (char c1 = str[1]; c1 == '<' || c1 == '^' || c1 == '>') {
            this->fill = str[0];
            alignChar = c1;
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

        // parse field width:
        if (i::isdigit(*str)) {
            unsigned w = 0;
            while (i::isdigit(*str)) {
                w = 10 * w + i::digittoint(*str++);
                if (w > kMaxWidth)
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
                if (p > kMaxPrecision)
                    throw format_error("invalid format spec: precision too large");
            }
            this->precision = uint8_t(p);
        }
        // parse "localized" specifier:
        if (*str == 'L') {
            this->localize = true;
            ++str;
        }
        // parse type code:
        if (char t = *str; t != '}') {
            if (!i::isalpha(t))
                throw format_error("invalid format spec: invalid type character");
            if (argType != i::ArgType::None) {
                const char *valid = i::kValidTypeCharsForArgType[uint8_t(argType)];
                while (*valid && *valid != t)
                    ++valid;
                if (!*valid)
                    throw format_error("invalid format spec: invalid spec for argument");
            }
            this->type = t;
            if (str[1] != '}')
                throw format_error("invalid format spec: unknown chars at end");
        }
    }

}
