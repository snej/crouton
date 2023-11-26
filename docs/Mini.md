# MiniLogger, MiniFormat, MiniOStream

These utilities are simplified versions of [spdlog][SPDLOG], `std::format()` and `std::ostream`. They're designed to provide the most useful functionality, with a compatible subset API, but a _much_ smaller code footprint (~16KB) suitable for embedded devices.

The C++ iostream library is well-known to be large and complex (as well as slow.) The [ESP32 microcontroller documentation][ESPDOC] warns "Simply including `<iostream>` header in one of the source files significantly increases the binary size by about 200 kB."

C++20's `std::format`, and the spdlog logging library that uses it, are fast; but some of that speed comes from aggressively inlining all the formatting calls, which generates a lot of code. Anecdotally, after switching from a custom log API to spdlog in a smallish C++ project of mine, I found its binary size increased by about 150KB. The link-map implied that most of this overhead came from a multitude of inlined `std::format` calls.

By contrast, a simple "`hello, {} world`" program using MiniLogger compiles to about 16KB of code on ARM64 macOS. And adding more `format` or logging calls won't bloat the code size because the formatting isn't inlined.

> In case you’re curious: MiniFormat performs one level of inlining, passing the arguments to a single non-templated core function using plain old C “…” varargs. A hidden extra argument contains a synthesized list of the arguments’ types, so the implementation function can pull them out safely with `va_arg`.

## Using Them

For now the source code is part of [Crouton](README.md), but separable. There are three header files in [include/crouton/util](../include/crouton/util/), and three source files in [src/support](../src/support/). They have no dependencies on the rest of Crouton; you should be able to copy these to your own project and build and use them.

Like the rest of Crouton, these require C++20. They build with Clang 15, GCC 12 or Visual Studio 17 (2022). They have been tested on macOS, Ubuntu Linux and ESP32 microcontrollers.

## MiniLogger

MiniLogger is based on a subset of the popular [spdlog][SPDLOG] library. It provides a `logger` class that knows how to write log messages. Each instance has a name that identifies what subsystem or module it pertains to; this is written along with the message. Each instance also has a level, which determines the minimum severity of message it will write.

It's best to create loggers at initialization time and expose them as global variables, for example
```c++
    logger NetLog("net", level::info);
```
You can then log messages to it like `NetLog.info("opening socket to {}:{}", host, port);`. The format string has the same syntax as MiniFormat (below).

You can make logging more or less verbose by setting the loggers' levels. A logger whose level is set to `warn` will only emit warning, error or critical messages.

A convenient way to configure levels is to call `logger::load_env_levels()` after creating loggers. This will read the environment variable `CROUTON_LOG_LEVEL` and, if it exists, set loggers' levels based on its value:
- First, the string is split into sections at commas.
- A section of the form `name=levelname` sets the level of the named logger. Level names are trace, debug, info, warn, err, critical, off.
- If there's no logger with that name, the level will be applied when a logger with that name is created.
- A section that's just "levelname" applies to all loggers that aren't explicitly named.

By default, log messages are written to stderr. Each message contains a timestamp, the thread ID, the logger name, the log level, and the formatted message.

If you want to write the messages yourself, call `logger::set_output()` at startup and pass your own callback function.

### Missing Features

- Custom "sink" types
- Custom formatting of log output
- Variety of built-in sink types including log files, rotation of files, etc.
- Probably other deeper parts of spdlog I haven't used


## MiniFormat

MiniFormat is the most complex piece, but its API boils down to two functions:

- `format("formatstring", args...)` formats its arguments according to the format string literal, and returns the result as a string.
- `format_to(ostream, "formatstring", args...)` writes the formatted output to the given stream.

### Format string syntax

The format string has the same syntax as `std::format`, which is in turn based on Python syntax. It's also similar to `printf` but with curly braces instead of percent signs. The _format specifiers_ in the string are replaced by formatted argument values.

- A format specifier begins with `{` and ends with `}`.
- If you need to put a literal curly-brace in the output, just put two in a row: `{{` produces an open brace and `}}` produces a close brace.
- The simplest specifier is `{}`. This just writes the argument with default formatting. Most of the time this is all you need.
- Nontrivial specifiers start with `{:` and then contain a printf-style format like `+08d` or `.20s`. Note that the type character (`d`, `s`, etc.) is optional; if it's omitted you get a default type based on the argument type.
- For more details, look up the "Standard Format Specification" in the [C++ standard library reference][FMTSPEC]

Arguments to `format` can be numeric, bool, char, any type of string (`char*`, `string`, `string_view`), raw pointers, and also any type that can be written to an `ostream` via `<<`. (That's the Mini `ostream`, not `std::ostream`.)

### Safety and errors

Unlike the `printf` functions, `format` is safe. It checks the format string and arguments _at compile time_ and produces an error if the syntax is invalid, the types don't match, or there are insufficient arguments.

Unfortunately the compile-time errors aren't exactly clear. Clang and GCC report an exception in a compile-time (consteval) function with a message like "call to consteval function `crouton::mini::FormatString_<int, int>::FormatString_` is not a constant expression". It's telling you that `throw` isn't allowed in compile-time code, which is true, but it would be a lot more useful if it showed you the exception's message!

To diagnose this, look at the line the compiler points to as the invalid subexpression: it'll be a `throw` statement, whose message should give you a clue. In the example below, the format call has four arg specifiers but only two arguments, which is of course illegal:

```
test_mini.cc:144:24: error: call to consteval function 'crouton::mini::FormatString_<int, int>::FormatString_' is not a constant expression
    CHECK(mini::format("One {} two {} three {} four {}", 1, 2)
                       ^
In file included from /Users/snej/Projects/crouton/tests/test_mini.cc:19:
In file included from /Users/snej/Projects/crouton/tests/tests.hh:19:
In file included from include/crouton/Crouton.hh:34:
In file included from include/crouton/util/Logging.hh:28:
include/crouton/util/MiniFormat.hh:359:21: note: subexpression not valid in a constant expression
                    throw format_error("More format specifiers than arguments");
                    ^
```

### Missing Features

- No input (parsing), only output.
- You can't create custom formatters that interpret custom field specs. Instead, implement `operator<<(mini::ostream&, T)` for your type T.
- Arguments can't be reordered: i.e. a field spec like `{nn:}` isn't allowed.
- Field widths & alignment are not Unicode-aware: they assume 1 byte == 1 space.
- Localized variants are unimplemented: using 'L' in a format spec has no effect.
- Only 10 arguments are allowed. (You can change this by changing `BaseFormatString::kMaxSpecs`.)
- Field width and precision are limited to 255.

### Known bugs:

- When a number is zero-padded, the zeroes are written before the sign character, not after it.
- When the alternate ('#') form of a float adds a decimal point, it's written after any exponent,
  when it should go before.
- On macOS versions prior to 13.3, or iOS before 16.3, floating-point values will be written in default format, ignoring the precision or type in the format spec. (This is because the necessary C++17 `std::to_chars` functions weren't added to Apple's libc++ until then. As a workaround, it just calls `snprintf` with a `%g` format.)


## MiniOStream

MiniOStream provides a very basic `ostream`, a minimal abstract base class with a pure virtual `write` method and a no-op virtual `flush` method. Concrete subclasses are

- `fdstream` -- writes to a stdio `FILE*`. There are two global instances, `cout` and `cerr`.
- `stringstream` -- like its std equivalent, writes to a string.
- `bufstream` -- writes to an external buffer, a consecutive range of memory provided by the caller.
  If the buffer would overflow, it throws an exception.
- `owned_bufstream` -- subclass of `bufstream` that owns its buffer. You provide the buffer size as a template parameter. Buffers smaller than 64 bytes are inlined in the object; larger buffers are heap-allocated.

In addition, the usual `<<` operator is implemented. It can write numbers, characters, strings, pointers, and spans of bytes.

As usual, you can define your own `<<` overloads to write your own types. The concept `ostreamable` defines any type that can be written to an `ostream`.

> Note: This `ostream` is of course unrelated to `std::ostream`. If you have existing `<<` overrides they'll be ignored by MiniOStream; but it's easy enough to write wrappers.

### Missing Features

Most of them! Seriously, this is strictly a "do the simplest thing that could possibly work" implementation, with just enough functionality to support MiniFormat.

[ESPDOC]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/cplusplus.html#iostream
[FMTSPEC]: https://en.cppreference.com/w/cpp/utility/format/formatter
[SPDLOG]: https://github.com/gabime/spdlog
