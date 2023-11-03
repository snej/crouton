//
// Logging.hh
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
#include "crouton/util/Base.hh"
#include "crouton/util/MiniFormat.hh"

// By default, Crouton uses its own small logging library.
// To make it use spdlog instead, pre-define the macro `CROUTON_USE_SPDLOG` to `1`.
#ifndef CROUTON_USE_SPDLOG
#define CROUTON_USE_SPDLOG 0
#endif

#if CROUTON_USE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>    // Makes custom types loggable via `operator <<` overloads
#endif

namespace crouton {

#if CROUTON_USE_SPDLOG
    using LoggerRef = spdlog::logger*;
    namespace LogLevel = spdlog::level;
    using LogLevelType = spdlog::level::level_enum;
#else
    namespace LogLevel {
        enum level_enum {
            trace, debug, info, warn, err, critical, off
        };
    };
    using LogLevelType = LogLevel::level_enum;

    class Logger {
    public:
        Logger(string name, LogLevelType level);
        ~Logger() = delete;

        string const& name() const Pure                 {return _name;}
        LogLevelType level() const Pure                 {return _level;}
        void set_level(LogLevelType level)              {_level = level;}
        bool should_log(LogLevelType level) const Pure  {return level >= _level;}

        template<minifmt::Formattable... Args>
        void log(LogLevelType lvl, string_view fmt, Args &&...args) {
            if (should_log(lvl)) [[unlikely]]
                _log(lvl, fmt, minifmt::FmtIDs<Args...>::ids, minifmt::passArg(args)...);
        }

        void log(LogLevelType lvl, string_view msg);

        template<minifmt::Formattable... Args>
        void trace(string_view fmt, Args &&...args) {
            log(LogLevel::trace, fmt, std::forward<Args>(args)...);
        }
        template<minifmt::Formattable... Args>
        void debug(string_view fmt, Args &&...args) {
            log(LogLevel::debug, fmt, std::forward<Args>(args)...);
        }
        template<minifmt::Formattable... Args>
        void info(string_view fmt, Args &&...args) {
            log(LogLevel::info, fmt, std::forward<Args>(args)...);
        }
        template<minifmt::Formattable... Args>
        void warn(string_view fmt, Args &&...args) {
            log(LogLevel::warn, fmt, std::forward<Args>(args)...);
        }
        template<minifmt::Formattable... Args>
        void error(string_view fmt, Args &&...args) {
            log(LogLevel::err, fmt, std::forward<Args>(args)...);
        }
        template<minifmt::Formattable... Args>
        void critical(string_view fmt, Args &&...args) {
            log(LogLevel::critical, fmt, std::forward<Args>(args)...);
        }

        static void load_env_levels();

        static void apply_all(std::function<void(Logger&)>);

    private:
        void _log(LogLevelType, string_view fmt, minifmt::FmtIDList, ...);
        void _writeHeader(LogLevelType);

        string const _name;
        LogLevelType _level;
    };

    using LoggerRef = Logger*;
#endif

    /*
     You can configure the log level(s) by setting the environment variable `SPDLOG_LEVEL`.
     For example:

        * Set global level to debug:
            `export SPDLOG_LEVEL=debug`
        * Turn off all logging except for logger1:
            `export SPDLOG_LEVEL="*=off,logger1=debug"`
        * Turn off all logging except for logger1 and logger2:
            `export SPDLOG_LEVEL="off,logger1=debug,logger2=info"`

        For much more information about spdlog, see docs at https://github.com/gabime/spdlog/
     */


    /// Initializes logging, sets log levels and creates well-known loggers.
    /// Called automatically by `MakeLogger` and `AddSink`.
    /// Calling this multiple times has no effect.
    /// @note You can set the environment variable `CROUTON_LOG_LEVEL` (or `SPDLOG_LEVEL`, if using
    ///     spdlog) to configure log levels. The value is a series of comma-separated items.
    ///     An item that's just a log level name (as in level_enum) sets all loggers to that level.
    ///     An item of the form "name=level" sets only the named logger to that level.
    void InitLogging();


    /// Well-known loggers:
    extern LoggerRef
        Log,    // Default logger
        LCoro,  // Coroutine lifecycle
        LSched, // Scheduler
        LLoop,  // Event-loop
        LNet;   // Network I/O


    /// Creates a new spdlog logger.
    LoggerRef MakeLogger(string_view name, LogLevelType = LogLevel::info);

#if CROUTON_USE_SPDLOG
    /// Creates a log destination.
    void AddSink(spdlog::sink_ptr);
#else
    using LogSink = void (*)(Logger const&, LogLevelType, string_view) noexcept;

    /// Redirects log output from stderr to the given callback.
    void SetLogOutput(LogSink);
#endif
}
