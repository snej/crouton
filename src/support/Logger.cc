//
// Logger.cc
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

#include "crouton/util/Logging.hh"

#if ! CROUTON_USE_SPDLOG

#include "crouton/util/Logger.hh"
#include "crouton/io/Process.hh"
#include "support/StringUtils.hh"

#include <cstring>
#include <mutex>

#ifdef ESP_PLATFORM
#  include <esp_log.h>
#else
#  include <chrono>
#  include <ctime>
#  include <iomanip>
#  include "crouton/util/MiniOStream.hh"
#endif

namespace crouton::log {
    using namespace std;
    using namespace crouton::mini;

    static constexpr string_view kLevelName[] = {
        "trace", "debug", "info", "warn", "error", "critical", "off"
    };
    static constexpr const char* kLevelDisplayName[] = {
        "trace", "debug", "info ", "WARN ", "ERR  ", "CRITICAL", ""
    };


    static mutex            sLogMutex;          // Thread-safety & prevents overlapping msgs
    static vector<logger*>* sLoggers;           // All registered Loggers
    static logger::Sink     sLogSink = nullptr; // Optional function to write messages
    static const char*      sEnvLevelsStr;




    logger::logger(string name, level::level_enum level)
    :_name(std::move(name))
    ,_level(level) {
        unique_lock lock(sLogMutex);
        load_env_level();
        if (!sLoggers)
            sLoggers = new vector<logger*>;
        sLoggers->push_back(this);
    }


    logger* logger::get(string_view name) {
        unique_lock lock(sLogMutex);
        if (sLoggers) {
            for (auto logger : *sLoggers)
                if (logger->name() == name)
                    return logger;
        }
        return nullptr;
    }

    
    void logger::apply_all(std::function<void(logger&)> fn) {
        unique_lock lock(sLogMutex);
        if (sLoggers) {
            for (auto logger : *sLoggers)
                fn(*logger);
        }
    }


    void logger::set_output(logger::Sink sink) {
        unique_lock lock(sLogMutex);
        sLogSink = sink;
    }


    static level::level_enum levelNamed(string_view name) {
        int level = 0;
        for (string_view levelStr : kLevelName) {
            if (name == levelStr)
                return level::level_enum(level);
            ++level;
        }
        return log::level::info; // default if unrecognized name
    }


    void logger::load_env_levels(const char *envValue) {
        unique_lock lock(sLogMutex);
        if (!sEnvLevelsStr)
            sEnvLevelsStr = strdup(envValue ? envValue : "");
        for (auto logger : *sLoggers)
            logger->load_env_level();
    }


    void logger::load_env_level() {
        if (!sEnvLevelsStr)
            return; // Do nothing if load_env_levels() hasn't been called
        string_view rest(sEnvLevelsStr);
        while (!rest.empty()) {
            string_view item;
            tie(item,rest) = split(rest, ',');
            auto [k, v] = split(item, '=');
            if (v.empty()) {
                set_level(levelNamed(k));
            } else if (k == _name) {
                set_level(levelNamed(v));
                break;
            }
        }
    }


#ifndef ESP_PLATFORM
    static time_t       sTime;          // Time in seconds that's formatted in sTimeBuf
    static char         sTimeBuf[30];   // Formatted timestamp, to second accuracy


    void logger::load_env_levels() {
        load_env_levels(getenv("CROUTON_LOG_LEVEL"));
    }


    void logger::_writeHeader(level::level_enum lvl) {
        // sLogMutex must be locked
        io::TTY const& tty = io::TTY::err();
        timespec now;
        timespec_get(&now, TIME_UTC);
        if (now.tv_sec != sTime) {
            sTime = now.tv_sec;
            tm nowStruct;
#ifdef _MSC_VER
            nowStruct = *localtime(&sTime);
#else
            localtime_r(&sTime, &nowStruct);
#endif
            strcpy(sTimeBuf, "▣ ");
            strcat(sTimeBuf, tty.dim);
            size_t len = strlen(sTimeBuf);
            strftime(sTimeBuf + len, sizeof(sTimeBuf) - len, "%H:%M:%S.", &nowStruct);
        }

        const char* color = "";
        if (lvl >= log::level::err)
            color = tty.red;
        else if (lvl == log::level::warn)
            color = tty.yellow;

        fprintf(stderr, "%s%06ld%s %s%s| <%s> ",
                sTimeBuf, now.tv_nsec / 1000, tty.reset,
                color, kLevelDisplayName[int(lvl)], _name.c_str());
    }


    void logger::log(level::level_enum lvl, string_view msg) {
        if (should_log(lvl)) {
            if (auto sink = sLogSink) {
                sink(*this, lvl, msg);
            } else {
                unique_lock<mutex> lock(sLogMutex);
                _writeHeader(lvl);
                cerr << msg << io::TTY::err().reset << endl;
            }
        }
    }


    void logger::_log(level::level_enum lvl, BaseFormatString const& fmt, mini::ArgTypeList types, ...) {
        if (auto sink = sLogSink) {
            stringstream out;
            va_list args;
            va_start(args, types);
            mini::vformat_types_to(out, fmt, types, args);
            va_end(args);
            sink(*this, lvl, out.str());
        } else {
            unique_lock<mutex> lock(sLogMutex);

            _writeHeader(lvl);
            va_list args;
            va_start(args, types);
            mini::vformat_types_to(cerr, fmt, types, args);
            va_end(args);
            cerr << io::TTY::err().reset << endl;
        }
    }

#else // ESP_PLATFORM

    static constexpr esp_log_level_t kESPLevel[] = {
        ESP_LOG_VERBOSE, ESP_LOG_DEBUG, ESP_LOG_INFO, ESP_LOG_WARN, ESP_LOG_ERROR, ESP_LOG_NONE
    };
    static const char* kESPLevelChar = "TDIWE-";


    void logger::load_env_levels() { }


    void logger::log(level::level_enum lvl, string_view msg) {
        if (should_log(lvl) && kESPLevel[lvl] <= esp_log_level_get("Crouton")) {
            io::TTY const& tty = io::TTY::err();
            const char* color;
            switch (lvl) {
                case log::level::critical:
                case log::level::err:     color = tty.red; break;
                case log::level::warn:    color = tty.yellow; break;
                case log::level::debug:
                case log::level::trace:   color = tty.dim; break;
                default:                color = ""; break;
            }
#if CONFIG_LOG_TIMESTAMP_SOURCE_RTOS
            esp_log_write(kESPLevel[lvl], "Crouton", "%s%c (%4ld) <%s> %.*s%s\n",
                          color,
                          kESPLevelChar[lvl],
                          esp_log_timestamp(),
                          _name.c_str(),
                          int(msg.size()), msg.data(),
                          tty.reset);
#else
            esp_log_write(kESPLevel[lvl], "Crouton", "%s%c (%s) <%s> %.*s%s\n",
                          color,
                          kESPLevelChar[lvl],
                          esp_log_system_timestamp(),
                          _name.c_str(),
                          int(msg.size()), msg.data(),
                          tty.reset);
#endif
        }
    }


    void logger::_log(level::level_enum lvl, BaseFormatString const& fmt, mini::ArgTypeList types, ...) {
        va_list args;
        va_start(args, types);
        string message = mini::vformat_types(fmt, types, args);
        va_end(args);
        log(lvl, message);
    }
#endif // ESP_PLATFORM

}

#endif // ! CROUTON_USE_SPDLOG
