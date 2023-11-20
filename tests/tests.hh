//
// tests.hh
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

#include "crouton/Crouton.hh"
#include "crouton/util/Logging.hh"
#include "crouton/util/MiniOStream.hh"

#include "catch_amalgamated.hpp"

using namespace crouton;
using namespace crouton::mini;


// Runs a coroutine that returns `Future<void>`, returning once it's completed.
void RunCoroutine(std::function<Future<void>()>);

// Reads the contents of a file into a string.
Future<string> ReadFile(string const& path);
