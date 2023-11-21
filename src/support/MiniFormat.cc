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

namespace crouton::mini {
    using namespace std;
    using namespace i;

    static bool vformat_arg(ostream &out, i::FmtID itype, va_list &args) {
        switch( itype ) {
            case FmtID::None:       out << "{{{TOO FEW ARGS}}}"; return false;
            case FmtID::Bool:       out << (va_arg(args, int) ? "true" : "false"); break;
            case FmtID::Char:       out << char(va_arg(args, int)); break;
            case FmtID::Int:        out << va_arg(args, int); break;
            case FmtID::UInt:       out << va_arg(args, unsigned int); break;
            case FmtID::Long:       out << va_arg(args, long); break;
            case FmtID::ULong:      out << va_arg(args, unsigned long); break;
            case FmtID::LongLong:   out << va_arg(args, long long); break;
            case FmtID::ULongLong:  out << va_arg(args, unsigned long long); break;
            case FmtID::Double:     out << va_arg(args, double); break;
            case FmtID::CString:    out << va_arg(args, const char*); break;
            case FmtID::Pointer:    out << va_arg(args, const void*); break;
            case FmtID::String:     out << *va_arg(args, const string*); break;
            case FmtID::StringView: out << *va_arg(args, const string_view*); break;
            case FmtID::Arg:        va_arg(args, arg).writeTo(out); break;
        }
        return true;
    }


    void vformat_types(ostream& out, FormatString const& fmt, FmtIDList types, va_list args) {
        auto itype = types;

        for (size_t p = 0; p < fmt.size(); ++p) {
            string_view part = fmt[p];
            assert(!part.empty());
            switch (part[0]) {
                case '{':
                    if (part[1] == '{')
                        out << '{';
                    else if (!vformat_arg(out, *itype++, args))
                        return;
                    break;
                case '}':
                    out << '}';
                    break;
                default:
                    out << part;
                    break;
            }
        }

        // If there are more args than specifiers, write the rest as a comma-separated list:
        const char* delim = " : ";
        while (*itype != FmtID::None) {
            out << delim;
            delim = ", ";
            if (!vformat_arg(out, *itype++, args))
                return;
        }
    }


    void format_types(ostream& out, FormatString const& fmt, FmtIDList types, ...) {
        va_list args;
        va_start(args, types);
        vformat_types(out, fmt, types, args);
        va_end(args);
    }

    string vformat_types(FormatString const& fmt, FmtIDList types, va_list args) {
        stringstream out;
        vformat_types(out, fmt, types, args);
        return out.str();
    }

    string format_types(FormatString const& fmt, FmtIDList types, ...) {
        va_list args;
        va_start(args, types);
        string result = vformat_types(fmt, types, args);
        va_end(args);
        return result;
    }

}
