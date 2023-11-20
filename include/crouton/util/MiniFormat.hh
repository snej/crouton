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

        class arg;

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


    namespace i {
        // Struct that type-erases an `ostreamable` value; passed as arg to formatting fns.
        class arg {
        public:
            template <ostreamable T>
            explicit arg(T &&value)
            :_ptr(reinterpret_cast<const void*>(&value))
            ,_write(&writeFn<std::remove_cvref_t<T>>)
            { }

            explicit arg(va_list& args)
            :_ptr(va_arg(args, const void*))
            ,_write(va_arg(args, writeFn_t))
            { }

            void writeTo(ostream& out) const                        {_write(out, _ptr);}

            // for compatibility with std::fmt or spdlog, this object can itself be written
            //friend ostream& operator<< (ostream& out, arg const& f) {f.writeTo(out); return out;}
        private:
            using writeFn_t = void (*)(ostream&, const void*);

            template <typename T>
            static void writeFn(ostream& out, const void* ptr) { out << *(const T*)ptr; }

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
        auto passArg(T const& t)                            {return arg{t};}
    }


    // The core formatting functions:
    void format_types(ostream&, string_view fmt, FmtIDList types, ...);
    void vformat_types(ostream&, string_view fmt, FmtIDList types, va_list);
    string format_types(string_view fmt, FmtIDList types, ...);
    string vformat_types(string_view fmt, FmtIDList types, va_list);


    /** Writes formatted output to an ostream.
        @param out  The stream to write to.
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    void format(ostream& out, string_view fmt, Args &&...args) {
        format_types(out, fmt, FmtIDs<Args...>::ids, i::passArg(args)...);
    }


    /** Returns a formatted string..
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    string format(string_view fmt, Args &&...args) {
        return format_types(fmt, FmtIDs<Args...>::ids, i::passArg(args)...);
    }
}
