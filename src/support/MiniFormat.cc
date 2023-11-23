//
// MiniFormat.cc
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

#include "crouton/util/MiniFormat.hh"
#include "crouton/util/MiniOStream.hh"
#include <charconv>

namespace crouton::mini {
    using namespace std;
    using namespace i;


    BaseFormatString::iterator::iterator(BaseFormatString const& fmt)
    :_str(fmt._impl._str)
    ,_pLength(&fmt._impl._lengths[0])
    ,_pSpec(&fmt._impl._specs[0])
    { }


    string_view BaseFormatString::iterator::literal() const {
        assert(isLiteral());
        if (_str[0] == '{' || _str[0] == '}') [[unlikely]]
            return {_str, 1};
        else
            return {_str, *_pLength};
    }


    BaseFormatString::iterator& BaseFormatString::iterator::operator++ () {
        if (!isLiteral())
            ++_pSpec;
        _str += *_pLength;
        ++_pLength;
        return *this;
    }


    static constexpr BaseFormatString::Spec kDefaultSpec {};


    static bool isupper(char c) {
        return c >= 'A' && c <= 'Z';
    }

    static char toupper(char c) {
        return (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }

    static void upperize(char *begin, char *end) {
        for (char *c = begin; c != end; ++c)
            *c = char(toupper(*c));
    }


    static int baseForInt(ostream &out, BaseFormatString::Spec const& spec) {
        switch (spec.type) {
            case 'd': case 0: 
                return 10;
            case 'b': case 'B':
                if (spec.alternate)
                    out << '0' << spec.type;
                return 2;
            case 'o': 
                return 8;
            case 'x': case 'X':
                if (spec.alternate)
                    out << '0' << spec.type;
                return 16;
            case 'c': 
                return 0;
            default:  
                throw format_error("invalid type for integer arg");
        }
    }


    static void writeNonNegativeSign(ostream& out, BaseFormatString::sign_t mode) {
        using enum BaseFormatString::sign_t;
        if (mode == minusPlus)
            out << '+';
        else if (mode == minusSpace)
            out << ' ';
    }


    template <std::integral INT>
    static void vformat_integer(ostream &out,
                                BaseFormatString::Spec const& spec,
                                va_list &args)
    {
        INT i = va_arg(args, INT);
        if (spec.type == 'c') {
            if (i < 0 || i > 127)
                throw format_error("value out of range for {:c} format specifier");
            out << static_cast<char>(i);
            return;
        }

        if (i >= 0)
            writeNonNegativeSign(out, spec.sign);
        int base = baseForInt(out, spec);
        if (spec.alternate && base == 8 && i != 0)
            out << '0';
        char buf[64];
        auto result = std::to_chars(&buf[0], &buf[sizeof(buf)], i, base);
        assert(result.ec == std::errc{});
        if (spec.type == 'X')
            upperize(buf, result.ptr);
        out.write(&buf[0], size_t(result.ptr - buf));
    }


    static void vformat_double(ostream &out,
                               BaseFormatString::Spec const& spec,
                               double d)
    {
        if (d >= 0.0)
            writeNonNegativeSign(out, spec.sign);
        char buf[30];
#ifdef __APPLE__ // Apple's libc++ may not have the floating point versions of std::to_chars
        if (__builtin_available(macOS 13.3, iOS 16.3, tvOS 16.3, watchOS 9.3, *)) {
#endif

            std::to_chars_result result;
            if (spec.type == 0 && spec.precision == 255) {
                result = std::to_chars(&buf[0], &buf[sizeof(buf)], d);
            } else {
                std::chars_format format;
                int precision = (spec.precision != 255) ? spec.precision : 6;
                switch (spec.type) {
                    case 'a': case 'A': format = std::chars_format::hex; break;
                    case 'e': case 'E': format = std::chars_format::scientific; break;
                    case 'f': case 'F': format = std::chars_format::fixed; break;
                    case 0:
                    case 'g': case 'G': format = std::chars_format::general; break;
                    default:            throw format_error("invalid type for floating-point arg");
                }
                result = std::to_chars(&buf[0], &buf[sizeof(buf)], d, format, precision);
                assert(result.ec == std::errc{});
                if (isupper(spec.type))
                    upperize(buf, result.ptr);
            }

            if (spec.alternate) {
                if (string_view(buf, result.ptr).find('.') == string::npos)
                    *result.ptr++ = '.';    // append a '.' if there isn't one
            }

            out.write(&buf[0], result.ptr - buf);

#ifdef __APPLE__
        } else {
            // fallback if to_chars isn't available:
            snprintf(buf, sizeof(buf), "%g", d);
            out.write(buf);
        }
#endif
    }


    static void vformat_string(ostream &out,
                               BaseFormatString::Spec const& spec,
                               string_view str)
    {
        if (spec.type != 0 && spec.type != 's') [[unlikely]]
            throw format_error("invalid type for string arg");
        out << str;
    }


    static void vformat_arg_nowidth(ostream &out,
                                    BaseFormatString::Spec const& spec,
                                    i::ArgType itype,
                                    va_list &args)
    {
        switch( itype ) {
            case ArgType::Bool:
                if (spec.type == 0 || spec.type == 's') [[likely]] {
                    out << (va_arg(args, int) ? "true" : "false");
                    break;
                } else {
                    return vformat_integer<int>(out, spec, args);
                }
            case ArgType::Char:
                if (spec.type == 0 || spec.type == 'c') [[likely]] {
                    out << static_cast<char>(va_arg(args, int));
                    break;
                }
                [[fallthrough]]; // if a type is given, format as int:
            case ArgType::Int:        return vformat_integer<int>(out, spec, args);
            case ArgType::UInt:       return vformat_integer<unsigned int>(out, spec, args);
            case ArgType::Long:       return vformat_integer<long>(out, spec, args);
            case ArgType::ULong:      return vformat_integer<unsigned long>(out, spec, args);
            case ArgType::LongLong:   return vformat_integer<long long>(out, spec, args);
            case ArgType::ULongLong:  return vformat_integer<unsigned long long>(out, spec, args);
            case ArgType::Double:     return vformat_double(out, spec, va_arg(args, double));

            case ArgType::CString: {
                const char* str = va_arg(args, const char*);
                vformat_string(out, spec, str ? str : "");
                break;
            }
            case ArgType::String:
                vformat_string(out, spec, *va_arg(args, const string*));
                break;
            case ArgType::StringView:
                vformat_string(out, spec, *va_arg(args, const string_view*));
                break;

            case ArgType::Pointer:
                if (spec.type != 0 && spec.type != 'p' && spec.type != 'P') [[unlikely]]
                    throw format_error("invalid type for pointer arg");
                out << va_arg(args, const void*);
                break;
            case ArgType::Arg:
                va_arg(args, ostreamableArg).writeTo(out);
                break;
            case ArgType::None:
                throw format_error("too few format arguments");
        }
    }


    static void vformat_arg(ostream &out,
                            BaseFormatString::Spec const& spec,
                            i::ArgType itype,
                            va_list &args)
    {
        // The spec says "The width of a string is defined as the estimated number of
        // column positions appropriate for displaying it in a terminal" ... which gets
        // complicated when non-ASCII characters are involved. I'm ignoring this.
        //TODO: proper width computation

        if (spec.ordinary || (spec.width == 0 && spec.precision == 255)) [[likely]] {
            vformat_arg_nowidth(out, spec, itype, args);
        } else {
            // First format to a string:
            stringstream buf;
            vformat_arg_nowidth(buf, spec, itype, args);
            string str = buf.extract_str();
            size_t s = str.size();

            if (s >= spec.precision && (itype >= ArgType::CString && itype <= ArgType::StringView)) {
                // Clip string to the precision:
                out.write(str.data(), spec.precision);
            } else if (s >= spec.width) {
                // String is full width, write it all:
                out << str;
            } else {
                // String needs padding on one or both sides:
                auto pad = spec.width - s;
                auto align = spec.align;
                if (align == BaseFormatString::align_t::normal) {
                    if (itype >= Int && itype <= Double)
                        align = BaseFormatString::align_t::right;
                    else
                        align = BaseFormatString::align_t::left;
                }
                if (spec.align == BaseFormatString::align_t::center)
                    pad /= 2;
                if (spec.align != BaseFormatString::align_t::left) {
                    out << string(pad, spec.fill);
                    pad = spec.width - s - pad; // for centering
                }
                out << str;
                if (spec.align != BaseFormatString::align_t::right)
                    out << string(pad, spec.fill);
            }
        }
    }


    void vformat_types(ostream& out, BaseFormatString const& fmt, ArgTypeList types, va_list args) {
        auto itype = types;
        for (auto i = fmt.begin(); i != fmt.end(); ++i) {
            if (i.isLiteral())
                out << i.literal();
            else
                vformat_arg(out, i.spec(), *itype++, args);
        }
        // If there are more args than specifiers, write the rest as a comma-separated list:
        const char* delim = " : ";
        while (*itype != ArgType::None) {
            out << delim;
            delim = ", ";
            vformat_arg(out, kDefaultSpec, *itype++, args);
        }
    }


    void format_types(ostream& out, BaseFormatString const& fmt, ArgTypeList types, ...) {
        va_list args;
        va_start(args, types);
        vformat_types(out, fmt, types, args);
        va_end(args);
    }

    string vformat_types(BaseFormatString const& fmt, ArgTypeList types, va_list args) {
        stringstream out;
        vformat_types(out, fmt, types, args);
        return out.str();
    }

    string format_types(BaseFormatString const& fmt, ArgTypeList types, ...) {
        va_list args;
        va_start(args, types);
        string result = vformat_types(fmt, types, args);
        va_end(args);
        return result;
    }

}
