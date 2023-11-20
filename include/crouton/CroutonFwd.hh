//
// CroutonFwd.hh
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


/* Forward declarations of Crouton types. 
   To be #include'd in header files instead of Crouton.hh, when possible. */


/// Macros for declaring a function that returns a Future, e.g. `ASYNC<void> close();`
/// It's surprisingly easy to forget to await the Future, especially `Future<void>`,
/// hence the `[[nodiscard]]` annotation.
#define        ASYNC [[nodiscard("Future must be AWAITed or returned")]]         ::crouton::Future
#define  staticASYNC [[nodiscard("Future must be AWAITed or returned")]] static  ::crouton::Future
#define virtualASYNC [[nodiscard("Future must be AWAITed or returned")]] virtual ::crouton::Future


namespace crouton {

    struct Buffer;
    class CoCondition;
    class ConstBytes;
    class Error;
    class EventLoop;
    class MutableBytes;
    class Scheduler;
    class Select;
    class Suspension;
    class Task;
    class Timer;

    template <typename T> class AsyncQueue;
    template <typename T> class Blocker;
    template <typename T> class BoundedAsyncQueue;
    template <typename T, class Self> class Bytes;
    template <typename T> class Future;
    template <typename T> class FutureState;
    template <typename T> class Generator;
    template <typename T> class Publisher;
    template <typename T> class Result;
    template <typename T> class SeriesConsumer;
    template <typename T> class SeriesProducer;
    template <typename T> class Subscriber;

    template <typename T> using FutureProvider = std::shared_ptr<FutureState<T>>;

    namespace mini {
        class ostream;
        class sstream;
    }
}
