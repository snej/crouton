//
// Scheduler.hh
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
#include "crouton/Coroutine.hh"

#include <atomic>
#include <deque>
#include <functional>
#include <ranges>
#include <unordered_map>

namespace crouton {
    class EventLoop;
    class Suspension;
    class Task;


    /** Schedules coroutines on a single thread. Each thread has an instance of this.
        @warning The API is *not* thread-safe, except as noted. */
    class Scheduler {
    public:
        /// Returns the Scheduler instance for the current thread. (Thread-safe, obviously.)
        static Scheduler& current();

        /// True if this is the current thread's Scheduler. (Thread-safe.)
        bool isCurrent() const                          {return this == &current();}

        /// True if there are no tasks waiting to run.
        bool isIdle() const;

        bool isEmpty() const;

        /// Returns true if there are no coroutines ready or suspended, except possibly for the one
        /// belonging to the EventLoop. Checked at the end of unit tests.
        bool assertEmpty();

        //---- Event loop:

        /// Returns the associated event loop. If there is none, it creates one.
        EventLoop& eventLoop();

        /// Associates an existing EventLoop instance with this Scheduler/thread.
        void useEventLoop(EventLoop*);

        /// Runs the EventLoop indefinitely, until something calls stop on it.
        void run();

        /// Runs the event loop until the function returns true.
        /// The function is checked before each iteration of the loop.
        void runUntil(std::function<bool()> const& fn);

        /// Schedules the function to be run at the next iteration of the event loop.
        /// @note  This method is thread-safe.
        void onEventLoop(std::function<void()>);

        /// Schedules the function to be run at the next iteration of the event loop,
        /// and blocks until the function completes.
        /// @note  This method is thread-safe.
        /// @warning MUST NOT be called on the Scheduler's thread or it will deadlock.
        ///          Use `asapSync` instead if this is a possibility.
        void onEventLoopSync(std::function<void()>);

        /// Runs the lambda/function as soon as possible: either immediately if this Scheduler is
        /// current, else on its next event loop iteration.
        template <std::invocable FN>
        void asap(FN fn) {
            if (isCurrent()) {
                fn();
            } else {
                onEventLoop(fn);
            }
        }

        template <std::invocable FN>
        void asapSync(FN fn) {
            if (isCurrent()) {
                fn();
            } else {
                onEventLoopSync(fn);
            }
        }


        //==== Awaitable:

        class SchedAwaiter {
        public:
            explicit SchedAwaiter(Scheduler* sched)     :_sched(sched) { }
            bool await_ready() noexcept                 {return _sched->isCurrent();}
            coro_handle await_suspend(coro_handle h) noexcept {
                auto next = lifecycle::suspendingTo(h, CRTN_TYPEID(*_sched), _sched, nullptr);
                _sched->adopt(h);
                return next;
            }
            void await_resume() noexcept                {precondition(_sched->isCurrent());}
        private:
            Scheduler* _sched;
        };

        /// `co_await`ing a Scheduler moves the current coroutine to its thread.
        /// @warning  The coroutine's type must be thread-safe. `Future` is.
        SchedAwaiter operator co_await()                {return SchedAwaiter(this);}


        /// Called from "normal" code.
        /// Resumes the next ready coroutine and returns true.
        /// If no coroutines are ready, returns false.
        bool resume();

        //---- Coroutine management; mostly called from coroutine implementations

        /// Adds a coroutine handle to the end of the ready queue, where at some point it will
        /// be returned from next().
        void schedule(coro_handle h);

        /// Allows a running coroutine `h` to give another ready coroutine some time.
        /// Returns the coroutine that should run next, possibly `h` if no others are ready.
        coro_handle yield(coro_handle h);

        /// Removes the coroutine from the ready queue, if it's still in it.
        /// To be called from an `await_resume` method.  //TODO: Is this method still needed?
        void resumed(coro_handle h);

        /// Returns the coroutine that should be resumed, or else `dflt`.
        [[nodiscard]] coro_handle nextOr(coro_handle dflt);

        /// Adds a coroutine handle to the suspension set.
        /// To make it runnable again, call the returned Suspension's `wakeUp` method
        /// from any thread.
        [[nodiscard]] Suspension suspend(coro_handle h);

        /// Tells the Scheduler a coroutine is about to be destroyed, so it can manage it
        /// correctly if it's in the suspended set.
        void destroying(coro_handle h);

        /// Tells the Scheduler a coroutine is gone.
#ifdef NDEBUG
        void finished(coro_handle h) { }
#else
        void finished(coro_handle h);
#endif

    private:
        struct SuspensionImpl;
        friend class Suspension;
        friend void lifecycle::ended(coro_handle);

        Scheduler();
        ~Scheduler();
        static Scheduler& _create();
        EventLoop* newEventLoop();
        coro_handle eventLoopHandle();
        bool isReady(coro_handle h) const;
        bool isWaiting(coro_handle h) const;
        void _wakeUp();
        void wakeUp();
        bool hasWakers() const;
        void scheduleWakers();
        void adopt(coro_handle);

        using SuspensionMap = std::unordered_map<const void*,SuspensionImpl>;

        std::deque<coro_handle> _ready;                     // Coroutines that are ready to run
        std::unique_ptr<SuspensionMap> _suspended;          // Suspended/sleeping coroutines
        EventLoop*              _eventLoop = nullptr;       // My event loop
        std::atomic<bool>       _woke = false;              // True if a suspended is waking
        bool                    _ownsEventLoop = false;     // True if I created _eventLoop
    };



    /** Represents a coroutine that's been suspended by calling Scheduler::suspend().
        It will resume after `wakeUp` is called. */
    class Suspension {
    public:
        /// Default constructor creates an empty/null Suspension.
        Suspension()                                    :_impl(nullptr) { }

        Suspension(Suspension&& s) noexcept             :_impl(s._impl) {s._impl = nullptr;}
        Suspension& operator=(Suspension&& s) noexcept  {std::swap(_impl, s._impl); return *this;}
        ~Suspension()                                   {if (_impl) cancel();}

        explicit operator bool() const Pure {return _impl != nullptr;}

        coro_handle handle() const;

        /// Makes the associated suspended coroutine runnable again;
        /// at some point its Scheduler will return it from next().
        /// @note Calling this resets the Suspension to empty.
        /// @note Calling on an empty Suspension is a no-op.
        /// @note This may be called from any thread.
        void wakeUp();

        /// Removes the associated coroutine from the suspended set.
        /// @note Calling this resets the Suspension to empty.
        /// @note Calling on an empty Suspension is a no-op.
        void cancel();

    private:
        friend class Scheduler;
        explicit Suspension(Scheduler::SuspensionImpl* impl) noexcept :_impl(impl) { };
        Suspension(Suspension const&) = delete;

        Scheduler::SuspensionImpl* _impl;
    };



    /** General purpose Awaitable to return from `yield_value`.
        It does nothing, just allows the Scheduler to schedule another runnable task if any. */
    struct Yielder : public CORO_NS::suspend_always {
        coro_handle await_suspend(coro_handle h) noexcept {
            _handle = h;
            return lifecycle::yieldingTo(h, Scheduler::current().yield(h), false);
        }

        void await_resume() noexcept {
            Scheduler::current().resumed(_handle);
            _handle = nullptr;
        }

        ~Yielder() {
            if (_handle) {
                // If await_resume hasn't been called yet, then the coro is being destroyed.
                Scheduler::current().destroying(_handle);
            }
        }
    private:
        coro_handle _handle;
    };

}
