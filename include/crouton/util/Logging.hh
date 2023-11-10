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

// By default, Crouton uses its own small logging library, defined in Logger.hh.
// To make it use spdlog instead, pre-define the macro `CROUTON_USE_SPDLOG` to `1`.
#ifndef CROUTON_USE_SPDLOG
#define CROUTON_USE_SPDLOG 0
#endif

#include "crouton/util/Base.hh"
#include "crouton/util/MiniFormat.hh"

#if CROUTON_USE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>    // Makes custom types loggable via `operator <<` overloads
#else
#include "crouton/util/Logger.hh"
#endif

namespace crouton {

#if CROUTON_USE_SPDLOG
    namespace log = ::spdlog;
#endif


    /// Initializes logging, sets log levels and creates well-known loggers.
    /// Called automatically by `MakeLogger` and `AddSink`.
    /// Calling this multiple times has no effect.
    /// @note You can set the environment variable `CROUTON_LOG_LEVEL` (or `SPDLOG_LEVEL`, if using
    ///     spdlog) to configure log levels. The value is a series of comma-separated items.
    ///     An item that's just a log level name (as in level_enum) sets all loggers to that level.
    ///     An item of the form "name=level" sets only the named logger to that level.
    void InitLogging();


    /// Well-known loggers:
    extern log::logger
        *Log,    // Default logger
        *LCoro,  // Coroutine lifecycle
        *LSched, // Scheduler
        *LLoop,  // Event-loop
        *LNet;   // Network I/O


    /// Creates a new logger, or returns an existing one with that name.
    log::logger* MakeLogger(string_view name, log::level::level_enum = log::level::info);

#if CROUTON_USE_SPDLOG
    /// Creates a log destination.
    void AddSink(spdlog::sink_ptr);
#else
    /// Redirects log output from stderr to the given callback.
    void SetLogOutput(log::logger::Sink sink);
#endif
}
