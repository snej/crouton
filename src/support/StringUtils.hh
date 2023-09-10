//
// StringUtils.hh
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
#include <cstring>
#include <string>
#include <string_view>  
#include <cassert>

namespace crouton {

    /// Plain-ASCII version of `tolower`, with no nonsense about locales or ints.
    inline char toLower(char c) {
        if (c >= 'A' && c <= 'Z')
            c += 32;
        return c;
    }

    /// Plain-ASCII version of `toupper`, with no nonsense about locales or ints.
    inline char toUpper(char c) {
        if (c >= 'a' && c <= 'z')
            c -= 32;
        return c;
    }

    inline bool isAlphanumeric(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    inline bool isHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    inline bool isURLSafe(char c) {
        return isAlphanumeric(c) || strchr("-_.~", c) != nullptr;
    }

    inline int hexDigitToInt(char c) {
        if (c < 'A') 
            return c - '0';
        if (c < 'a') 
            return c - 'A' + 10;
        return c - 'a' + 10;
    }

    inline char asHexDigit(int n) {
        assert(n >= 0 && n < 16);
        if (n < 10) 
            return '0' + char(n);
        return 'A' + char(n - 10);
    }

    /// Lowercases a string.
    inline std::string toLower(std::string str) {
        for (char &c : str)
            c = toLower(c);
        return str;
    }

    inline bool equalIgnoringCase(std::string_view a, std::string_view b) {
        size_t len = a.size();
        if (len != b.size())
            return false;
        for (size_t i = 0; i < len; i++) {
            if (toLower(a[i]) != toLower(b[i]))
                return false;
        }
        return true;
    }

    inline std::pair<std::string_view,std::string_view>
    split(std::string_view str, char c) {
        if (auto p = str.find(c); p != std::string::npos)
            return {str.substr(0, p), str.substr(p + 1)};
        else
            return {str, ""};
    }

    inline void replaceStringInPlace(std::string &str,
                                            std::string_view substring,
                                            std::string_view replacement)
    {
        std::string::size_type pos = 0;
        while((pos = str.find(substring, pos)) != std::string::npos) {
            str.replace(pos, substring.size(), replacement);
            pos += replacement.size();
        }
    }

}
