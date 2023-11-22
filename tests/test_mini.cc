//
// test_mini.cc
//
// 
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

#include "tests.hh"
#include "crouton/util/MiniFormat.hh"


TEST_CASE("FormatString Spec", "[mini]") {
    InitLogging(); //FIXME: Put this somewhere where it gets run before any test
    using enum mini::FormatString::align_t;
    using enum mini::FormatString::sign_t;

    static constexpr struct {const char* str; mini::FormatString::Spec spec;} kTests[] = {
        {"{}", {}},
        {"{:}", {}},
        {"{:d}", {.type = 'd', .ordinary = false}},
        {"{:^}", {.align = center, .fill = ' ', .ordinary = false}},
        {"{:*>}", {.align = right, .fill = '*', .ordinary = false}},
        {"{:+0}", {.sign = minusPlus, .align = right, .fill = '0', .ordinary = false}},
        {"{:+3.4f}", {.sign = minusPlus, .width = 3, .precision = 4, .type = 'f', .ordinary = false}},
        {"{:+0.4z}", {.sign = minusPlus, .align = right, .fill = '0', .precision = 4, .type = 'z', .ordinary = false}},
    };
    for (auto const& test : kTests) {
        INFO("Testing " << test.str);
        mini::FormatString::Spec spec;
        spec.parse(test.str + 1);
        CHECK(spec == test.spec);
    }
}


TEST_CASE("FormatString", "[mini]") {
    {
        mini::FormatString fmt("hi");
        auto i = fmt.begin();
        CHECK(i.isLiteral());
        CHECK(i.literal() == "hi");
        ++i;
        CHECK(i == fmt.end());
    }
    {
        mini::FormatString fmt("{}");
        auto i = fmt.begin();
        CHECK(!i.isLiteral());
        CHECK(i.spec() == FormatString::Spec{});
        ++i;
        CHECK(i == fmt.end());
    }
    {
        mini::FormatString fmt("this is {:} a {}{} test");
        auto i = fmt.begin();
        CHECK(i.literal() == "this is ");
        CHECK(!(++i).isLiteral());
        CHECK(i.spec() == FormatString::Spec{});
        CHECK((++i).literal() == " a ");
        CHECK(!(++i).isLiteral());
        CHECK(i.spec() == FormatString::Spec{});
        CHECK(!(++i).isLiteral());
        CHECK(i.spec() == FormatString::Spec{});
        CHECK((++i).literal() == " test");
        CHECK(++i == fmt.end());
    }
    {
        mini::FormatString fmt("this is a {{ test }}");
        auto i = fmt.begin();
        CHECK(i.isLiteral());
        CHECK(i.literal() == "this is a ");
        CHECK((++i).isLiteral());
        CHECK(i.literal() == "{");
        CHECK((++i).isLiteral());
        CHECK(i.literal() == " test ");
        CHECK((++i).isLiteral());
        CHECK(i.literal() == "}");
    }
    {
        mini::FormatString fmt("{{Escaped ... {}!");
        auto i = fmt.begin();
        CHECK(i.isLiteral());
        CHECK(i.literal()== "{");
        CHECK((++i).isLiteral());
        CHECK(i.literal() == "Escaped ... ");
        CHECK(!(++i).isLiteral());
        CHECK(i.spec() == FormatString::Spec{});
        CHECK((++i).isLiteral());
        CHECK(i.literal() == "!");
    }
}


TEST_CASE("MiniFormat", "[mini]") {
    CHECK(mini::format("No placeholders") == "No placeholders");
    CHECK(mini::format("Escaped {{... {}!", 7) == "Escaped {... 7!");
    CHECK(mini::format("Escaped {{{{... {}!", 7) == "Escaped {{... 7!");
    CHECK(mini::format("Escaped {{{}!", 7) == "Escaped {7!");
    CHECK(mini::format("{{Escaped ... {}!", 7) == "{Escaped ... 7!");
    CHECK(mini::format("Escaped ... {}!{{", 7) == "Escaped ... 7!{");
    CHECK(mini::format("Escaped {{... {}! ...}}", 7) == "Escaped {... 7! ...}");

    CHECK(mini::format("{} {}", false, true) == "false true");
    CHECK(mini::format("char '{}', i16 {}, u16 {}, i32 {}, u32 {}, i {}, u {}",
                       'X', int16_t(-1234), uint16_t(65432), int32_t(123456789),
                       uint32_t(987654321), int(-1234567), unsigned(7654321))
          == "char 'X', i16 -1234, u16 65432, i32 123456789, u32 987654321, i -1234567, u 7654321");
    CHECK(mini::format("long {}, ulong {}, i64 {}, u64 {}",
                       12345678l, 87654321ul, int64_t(-12345678901234), uint64_t(12345678901234))
          == "long 12345678, ulong 87654321, i64 -12345678901234, u64 12345678901234");
    float f = 12345.125;
    double d = 3.1415926;
    CHECK(mini::format("float {}, double {}", f, d)
          == "float 12345.125, double 3.1415926");

    const char* cstr = "C string";
    string str = "C++ string";
    CHECK(mini::format("cstr '{}', C++ str '{}', string_view '{}'", cstr, str, string_view(str))
          == "cstr 'C string', C++ str 'C++ string', string_view 'C++ string'");
    cstr = nullptr;
    CHECK(mini::format("cstr '{}'", cstr)  == "cstr ''");

    // excess args:
    CHECK(mini::format("One {} two {} three", 1, 2, 3)
          == "One 1 two 2 three : 3");
    CHECK(mini::format("One {} two {} three", 1, 2, 3, "hi")
          == "One 1 two 2 three : 3, hi");
//    CHECK(mini::format("One {} two {} three {} four {}", 1, 2)
//          == "One 1 two 2 three {{{TOO FEW ARGS}}}"); // TODO: Re-enable with exception check

    coro_handle h = std::noop_coroutine();
    CHECK(mini::format("{}", mini::i::ostreamableArg::make(logCoro{h}))
          == "¢exit");
    CHECK(mini::format("{}", logCoro{h})
          == "¢exit");

    InitLogging();
    logCoro lc{h};
    Log->info("After '{}', should say ¢exit: {}", cstr, lc);
    Log->info("After '{}', should say ¢exit: {}", cstr, logCoro{h});
}


TEST_CASE("MiniFormat Ints", "[mini]") {
    static constexpr struct {mini::FormatString fmt; int n; string_view formatted;} kTests[] = {
        // bases:
        {"{}",      123456789,  "123456789"},
        {"{:d}",    123456789,  "123456789"},
        {"{:x}",    123456789,  "75bcd15"},
        {"{:X}",    123456789,  "75BCD15"},
        {"{:o}",    123456789,  "726746425"},
        {"{:b}",    123456789,  "111010110111100110100010101"},
        // char:
        {"{:c}",    65,         "A"},
        // alternates:
        {"{:#x}",   123456789,  "0x75bcd15"},
        {"{:#X}",   123456789,  "0X75BCD15"},
        {"{:#o}",   123456789,  "0726746425"},
        {"{:#o}",   0,          "0"},
        {"{:#b}",   123456789,  "0b111010110111100110100010101"},
        // signs:
        {"{:-d}",   123456789,  "123456789"},
        {"{:+d}",   123456789,  "+123456789"},
        {"{: d}",   123456789,  " 123456789"},
        {"{:-d}",  -123456789,  "-123456789"},
        {"{:+d}",  -123456789,  "-123456789"},
        {"{: d}",  -123456789,  "-123456789"},
        {"{:-d}",   0,          "0"},
        {"{:+d}",   0,          "+0"},
        {"{: d}",   0,          " 0"},
    };
    for (auto const& test : kTests) {
        INFO("Testing " << test.fmt.str());
        CHECK(format(test.fmt, test.n) == test.formatted);
    }
}


TEST_CASE("MiniFormat Floats", "[mini]") {
    static constexpr struct {mini::FormatString fmt; double n; string_view formatted;} kTests[] = {
        // formats:
        {"{}",      1234.5678,  "1234.5678"},
        {"{:f}",    1234.5678,  "1234.567800"},
        {"{:g}",    1234.5678,  "1234.57"},
        {"{:e}",    1234.5678,  "1.234568e+03"},
        {"{:E}",    1234.5678,  "1.234568E+03"},
        {"{:a}",    1234.5678,  "1.34a457p+10"},
        {"{:A}",    1234.5678,  "1.34A457P+10"},
        // alternates:
        {"{:#}",    123456789,  "123456789."},
        // low precision:
        {"{:.2}",     1234.5678,  "1.2e+03"},
        {"{:.2f}",    1234.5678,  "1234.57"},
        {"{:.2g}",    1234.5678,  "1.2e+03"},
        {"{:.2e}",    1234.5678,  "1.23e+03"},
        {"{:.2a}",    1234.5678,  "1.35p+10"},
        // high precision:
        {"{:.8}",     1234.5678,  "1234.5678"},
        {"{:.8f}",    1234.5678,  "1234.56780000"},
        {"{:.8g}",    1234.5678,  "1234.5678"},
        {"{:.8e}",    1234.5678,  "1.23456780e+03"},
        {"{:.8a}",    1234.5678,  "1.34a456d6p+10"},
        // signs:
        {"{:-}",   1234.5678,  "1234.5678"},
        {"{:+}",   1234.5678,  "+1234.5678"},
        {"{: }",   1234.5678,  " 1234.5678"},
        {"{:-}",  -1234.5678,  "-1234.5678"},
        {"{:+}",  -1234.5678,  "-1234.5678"},
        {"{: }",  -1234.5678,  "-1234.5678"},
        {"{:-}",   0.0,        "0"},
        {"{:+}",   0.0,        "+0"},
        {"{: }",   0.0,        " 0"},
    };
    for (auto const& test : kTests) {
        INFO("Testing " << test.fmt.str());
        CHECK(format(test.fmt, test.n) == test.formatted);
    }
}


TEST_CASE("MiniFormat Widths", "[mini]") {
    static constexpr struct {mini::FormatString fmt; int n; string_view formatted;} kTests[] = {
        // widths:
        {"{:1d}",   1234,  "1234"},
        {"{:4d}",   1234,  "1234"},
        {"{:8d}",   1234,  "    1234"},
        // zero padding:
        {"{:08d}",  1234,  "00001234"},
        // explicit alignment + fill:
        {"{:-<8d}", 1234,  "1234----"},
        {"{:->8d}", 1234,  "----1234"},
        {"{:-^8d}", 1234,  "--1234--"},
        {"{:-^9d}", 1234,  "--1234---"},  // <- asymmetric!
        // explicit alignment, default fill:
        {"{:<8d}",  1234,  "1234    "},
        {"{:>8d}",  1234,  "    1234"},
        {"{:^8d}",  1234,  "  1234  "},
        // with prefixes:
//        {"{:08d}", -1234,  "-00001234"},  //FIXME: Ugh, make this work right
    };
    for (auto const& test : kTests) {
        INFO("Testing " << test.fmt.str());
        CHECK(format(test.fmt, test.n) == test.formatted);
    }
}
