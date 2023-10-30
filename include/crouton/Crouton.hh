//
// Crouton.hh
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
#include "crouton/CoCondition.hh"
#include "crouton/Error.hh"
#include "crouton/EventLoop.hh"
#include "crouton/Future.hh"
#include "crouton/Generator.hh"
#include "crouton/Misc.hh"
#include "crouton/PubSub.hh"
#include "crouton/Queue.hh"
#include "crouton/Result.hh"
#include "crouton/Scheduler.hh"
#include "crouton/Select.hh"
#include "crouton/Task.hh"

#include "crouton/util/Bytes.hh"
#include "crouton/util/Logging.hh"

#include "crouton/io/AddrInfo.hh"
#include "crouton/io/HTTPConnection.hh"
#include "crouton/io/HTTPHandler.hh"
#include "crouton/io/ISocket.hh"
#include "crouton/io/Process.hh"
#include "crouton/io/URL.hh"
#include "crouton/io/WebSocket.hh"

#ifndef ESP_PLATFORM
#include "crouton/io/FileStream.hh"
#include "crouton/io/Filesystem.hh"
#include "crouton/io/LocalSocket.hh"
#include "crouton/io/Pipe.hh"
#endif
