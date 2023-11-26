//
// MiniLogger.hh
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
#include "MiniFormat.hh"
#include <functional>

namespace crouton::mini {
    using std::string;
    using std::string_view;

    // Minimal logging API, mostly compatible with spdlog

    namespace level {
        enum level_enum {
            trace, debug, info, warn, err, critical, off
        };
    };


    /** A named logger, which can have its own level. */
    class logger {
    public:
        /// Constructs a logger with a name and a default initial level.
        logger(string name, level::level_enum defaultLevel);
        ~logger() = delete;

        /// The logger's name.
        string const& name() const                       {return _name;}

        /// The logger's current level. Messages with a lower level will not be logged.
        level::level_enum level() const                  {return _level;}

        /// Sets the current level.
        void set_level(level::level_enum level)              {_level = level;}

        /// Returns true if the logger will log messages at the given level.
        bool should_log(level::level_enum level) const   {return level >= _level;}

        /// Logs a formatted message at the given level.
        template<mini::Formattable... Args>
        void log(level::level_enum lvl, mini::FormatString<Args...> const& fmt, Args &&...args) {
            if (should_log(lvl)) [[unlikely]]
                _log(lvl, fmt, mini::ArgTypes<Args...>::ids, mini::i::passArg(args)...);
        }

        /// Logs an unformatted message at the given level.
        void log(level::level_enum lvl, string_view msg);

        // The methods below all call `log` with the levels indicated by their names.

        template<mini::Formattable... Args>
        void trace(mini::FormatString<Args...> const& fmt, Args &&...args) {
            log(level::trace, fmt, std::forward<Args>(args)...);
        }
        void trace(string_view msg)                         {log(level::trace, msg);}

        template<mini::Formattable... Args>
        void debug(mini::FormatString<Args...> const& fmt, Args &&...args) {
            log(level::debug, fmt, std::forward<Args>(args)...);
        }
        void debug(string_view msg)                         {log(level::debug, msg);}

        template<mini::Formattable... Args>
        void info(mini::FormatString<Args...> const& fmt, Args &&...args) {
            log(level::info, fmt, std::forward<Args>(args)...);
        }
        void info(string_view msg)                          {log(level::info, msg);}

        template<mini::Formattable... Args>
        void warn(mini::FormatString<Args...> const& fmt, Args &&...args) {
            log(level::warn, fmt, std::forward<Args>(args)...);
        }
        void warn(string_view msg)                          {log(level::warn, msg);}

        template<mini::Formattable... Args>
        void error(mini::FormatString<Args...> const& fmt, Args &&...args) {
            log(level::err, fmt, std::forward<Args>(args)...);
        }
        void error(string_view msg)                         {log(level::err, msg);}

        template<mini::Formattable... Args>
        void critical(mini::FormatString<Args...> const& fmt, Args &&...args) {
            log(level::critical, fmt, std::forward<Args>(args)...);
        }
        void critical(string_view msg)                      {log(level::critical, msg);}

        /// Initializes levels based on the value of the `CROUTON_LOG_LEVEL` environment variable.
        /// @note  This just calls `load_env_levels(getenv("CROUTON_LOG_LEVEL"))`.
        /// @note  This function isn't available on ESP32 since it has no environment API.
        static void load_env_levels();

        /// Initializes levels based on the given string. 
        /// - First, the string is split into sections at commas.
        /// - A section of the form "name=levelname" sets the level of the named logger.
        /// - If no logger with that name exists yet, the level will be set when it's created.
        /// - A section that's just "levelname" applies to all loggers that aren't explicitly named.
        static void load_env_levels(const char *envValue);

        /// Returns the logger with the given name, else nullptr.
        static logger* get(string_view name);

        /// Calls a function on every logger.
        static void apply_all(std::function<void(logger&)>);

        using Sink = void (*)(logger const&, level::level_enum, string_view) noexcept;

        /// Registers a function that handles log messages.
        /// If non-null, every log message triggers a call to this function, instead of the
        /// default behavior of writing to `stderr`.
        static void set_output(Sink);

    private:
        void _log(level::level_enum, mini::BaseFormatString const& fmt, mini::ArgTypeList, ...);
        void _writeHeader(level::level_enum);
        void load_env_level();

        string const        _name;
        level::level_enum   _level;
    };

}
