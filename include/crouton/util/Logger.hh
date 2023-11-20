//
// Logger.hh
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

#if CROUTON_USE_SPDLOG != 0
#error "Don't include Logger.hh directly; include Logging.hh instead"
#endif

#pragma once
#include "crouton/util/Base.hh"
#include "crouton/util/MiniFormat.hh"
#include <functional>

namespace crouton::log {

    // Minimal logging API, mostly compatible with spdlog

    namespace level {
        enum level_enum {
            trace, debug, info, warn, err, critical, off
        };
    };


    class logger {
    public:
        logger(string name, level::level_enum level);
        ~logger() = delete;

        string const& name() const Pure                      {return _name;}
        level::level_enum level() const Pure                 {return _level;}
        void set_level(level::level_enum level)              {_level = level;}
        bool should_log(level::level_enum level) const Pure  {return level >= _level;}

        template<mini::Formattable... Args>
        void log(level::level_enum lvl, string_view fmt, Args &&...args) {
            if (should_log(lvl)) [[unlikely]]
                _log(lvl, fmt, mini::FmtIDs<Args...>::ids, mini::i::passArg(args)...);
        }

        void log(level::level_enum lvl, string_view msg);

        template<mini::Formattable... Args>
        void trace(string_view fmt, Args &&...args) {
            log(level::trace, fmt, std::forward<Args>(args)...);
        }

        template<mini::Formattable... Args>
        void debug(string_view fmt, Args &&...args) {
            log(level::debug, fmt, std::forward<Args>(args)...);
        }

        template<mini::Formattable... Args>
        void info(string_view fmt, Args &&...args) {
            log(level::info, fmt, std::forward<Args>(args)...);
        }

        template<mini::Formattable... Args>
        void warn(string_view fmt, Args &&...args) {
            log(level::warn, fmt, std::forward<Args>(args)...);
        }

        template<mini::Formattable... Args>
        void error(string_view fmt, Args &&...args) {
            log(level::err, fmt, std::forward<Args>(args)...);
        }

        template<mini::Formattable... Args>
        void critical(string_view fmt, Args &&...args) {
            log(level::critical, fmt, std::forward<Args>(args)...);
        }

        static void load_env_levels();

        static logger* get(string_view name);

        static void apply_all(std::function<void(logger&)>);

        using Sink = void (*)(logger const&, level::level_enum, string_view) noexcept;

        static void set_output(Sink);

    private:
        void _log(level::level_enum, string_view fmt, mini::FmtIDList, ...);
        void _writeHeader(level::level_enum);
        void load_env_level();

        string const        _name;
        level::level_enum   _level;
    };

}
