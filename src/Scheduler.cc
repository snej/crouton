//
// Scheduler.cc
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

#include "crouton/Scheduler.hh"
#include "crouton/EventLoop.hh"
#include "Internal.hh"
#include "crouton/util/Logging.hh"
#include "crouton/Task.hh"
#include <algorithm>

namespace crouton {
    using namespace std;


#pragma mark - SUSPENSION:


    struct Scheduler::SuspensionImpl {
    public:
        SuspensionImpl(coro_handle h, Scheduler *s) 
        :_handle(h), _scheduler(s) { }


        /// Makes the associated suspended coroutine runnable again;
        /// at some point its Scheduler will return it from next().
        /// @note This may be called from any thread, but _only once_.
        /// @warning  The Suspension pointer becomes invalid as soon as this is called.
        void wakeUp() {
            assert(_visible);
            if (_wakeMe.test_and_set() == false) {
                _visible = false;
                LSched->trace("{} unblocked", logCoro{_handle});
                lifecycle::ready(_handle);
                auto sched = _scheduler;
                assert(sched);
                _scheduler = nullptr;
                sched->wakeUp();
            }
        }

        /// Removes the associated coroutine from the suspended set.
        /// You must call this if the coroutine is destroyed while a Suspension exists.
        void cancel() {
            LSched->trace("{} Suspension canceled -- forgetting it", logCoro{_handle});
            assert(_visible);
            _handle = nullptr;
            if (_wakeMe.test_and_set() == false) {
                _visible = false;
                auto sched = _scheduler;
                assert(sched);
                _scheduler = nullptr;
                sched->wakeUp();
            }
        }

        coro_handle         _handle;                    // The coroutine (not really needed)
        Scheduler*          _scheduler;                 // Scheduler that owns coroutine
        std::atomic_flag    _wakeMe = ATOMIC_FLAG_INIT; // Indicates coroutine wants to wake up
        bool                _visible = false;           // Is this Suspension externally visible?
    };


    coro_handle Suspension::handle() const {
        return _impl ? _impl->_handle : coro_handle{};
    }

    void Suspension::wakeUp() {
        if (auto impl = _impl) {
            _impl = nullptr;
            impl->wakeUp();
        }
    }
    
    void Suspension::cancel() {
        if (auto impl = _impl) {
            _impl = nullptr;
            impl->cancel();
        }
    }


#pragma mark - SCHEDULER:


    Scheduler::Scheduler()
    :_suspended(new SuspensionMap)
    { 
        InitLogging();
        LSched->debug("Created Scheduler {}", (void*)this);
    }


    Scheduler::~Scheduler() {
        if (isEmpty())
            LSched->debug("Destructed Scheduler {}", (void*)this);
        else
            LSched->warn("Destructing Scheduler {} with {} ready, {} suspended coroutines",
                         (void*)this, _ready.size(), _suspended->size());
    }


    Scheduler& Scheduler::current() {
        static thread_local Scheduler sCurrent;
        return sCurrent;
    }

    
    /// True if there are no tasks waiting to run.
    bool Scheduler::isIdle() const {
        return !hasWakers() && _ready.empty();
    }

    bool Scheduler::isEmpty() const {
        return isIdle() && _suspended->empty();
    }

    /// Returns true if there are no coroutines ready or suspended, except possibly for the one
    /// belonging to the EventLoop. Checked at the end of unit tests.
    bool Scheduler::assertEmpty() {
        auto stackDepth = lifecycle::stackDepth();
        if (stackDepth > 0) {
            LSched->info("assertEmpty: Note: Ignoring coroutines in call stack: {}",
                         lifecycle::dumpStack());
        }

        scheduleWakers();
        auto coroCount = lifecycle::count() - stackDepth;
        if (isEmpty() && coroCount == 0)
            return true;
        if (coroCount > 0)
            LSched->info("There are {} coroutines (on all threads)", coroCount);
        LSched->info("Scheduler::assertEmpty: Running event loop until {} ready and {} suspended coroutines finish...",
                     _ready.size(), _suspended->size());
        int attempt = 0;    //TODO: Wait for a time interval, not attempt count
        const_cast<Scheduler*>(this)->runUntil([&] {
            if ((isEmpty() && lifecycle::count() - stackDepth == 0) || ++attempt >= 100)
                return true;
            LSched->info("Scheduler::assertEmpty: still waiting...");
            return false;
        });
        if (attempt < 100) {
            LSched->info("...OK, all coroutines finished now.");
            return true;
        }

        LSched->error("** Unexpected coroutines still in existence:");
        lifecycle::logAll();

        LSched->error("** On this Scheduler:");
        for (auto &r : _ready)
            LSched->info("ready: {}", logCoro{r});
        for (auto &s : *_suspended)
            LSched->info("\tsuspended: {}" , logCoro{s.second._handle});
        return false;
    }


    EventLoop& Scheduler::eventLoop() {
        if (!_eventLoop) {
            precondition(isCurrent());
            _eventLoop = newEventLoop();
            _ownsEventLoop = true;
        }
        return *_eventLoop;
    }

    void Scheduler::useEventLoop(EventLoop* loop) {
        precondition(isCurrent());
        assert(!_eventLoop);
        _eventLoop = loop;
        _ownsEventLoop = false;
    }

    void Scheduler::run() {
        runUntil([]{return false;});
    }

    void Scheduler::runUntil(std::function<bool()> const& fn) {
        while (!fn()) {
            bool idle = !resume();
            if (!idle && fn())
                break;
            eventLoop().runOnce(idle);
        }
    }

    void Scheduler::_wakeUp() {
        if (isCurrent()) {
            LSched->debug("wake up!");
            if (_eventLoop && _eventLoop->isRunning())
                _eventLoop->stop(false);    // efficient stop
        } else {
            LSched->debug("wake up! (from another thread)");
            assert(_eventLoop);
            _eventLoop->stop(true);         // thread-safe stop
        }
    }

    void Scheduler::onEventLoop(std::function<void()> fn) {
        eventLoop().perform(std::move(fn), false);
    }

    void Scheduler::onEventLoopSync(std::function<void()> fn) {
        precondition(!isCurrent());
        eventLoop().perform(std::move(fn), true);
    }

    EventLoop::~EventLoop() = default;


    void Scheduler::schedule(coro_handle h) {
        precondition(isCurrent());
        assert(!isWaiting(h));
        if (!isReady(h)) {
            LSched->debug("reschedule {} (behind {} others)",
                          logCoro{h}, _ready.size());
            _ready.push_back(h);
        }
    }

    void Scheduler::adopt(coro_handle h) {
        onEventLoop([this, h] { schedule(h); });
    }

    coro_handle Scheduler::yield(coro_handle h) {
        if (isIdle()) {
            return h;
        } else {
            schedule(h);
            return nullptr;
        }
    }

    void Scheduler::resumed(coro_handle h) {
        precondition(isCurrent());
        if (auto i = std::find(_ready.begin(), _ready.end(), h); i != _ready.end())
            _ready.erase(i);
    }

    coro_handle Scheduler::nextOr(coro_handle dflt) {
        precondition(isCurrent());
        scheduleWakers();
        if (_ready.empty()) {
            return dflt;
        } else {
            coro_handle h = _ready.front();
            _ready.pop_front();
            LSched->debug("resume {}", logCoro{h});
            return h;
        }
    }

    Suspension Scheduler::suspend(coro_handle h) {
        LSched->debug("suspend {}", logCoro{h});
        precondition(isCurrent());
        assert(!isReady(h));
        (void)eventLoop();  // Must have an event loop in order to wake up (see _wakeUp)
        auto [i, added] = _suspended->try_emplace(h.address(), h, this);
        i->second._visible = true;
        return Suspension(&i->second);
    }

    void Scheduler::destroying(coro_handle h) {
        LSched->debug("destroying {}", logCoro{h});
        precondition(isCurrent());
        if (auto i = _suspended->find(h.address()); i != _suspended->end()) {
            SuspensionImpl& sus = i->second;
            if (sus._wakeMe.test_and_set()) {
                // The holder of the Suspension already tried to wake it, so it's OK to delete it:
                _suspended->erase(i);
            } else {
                assert(!sus._visible);
                sus._handle = nullptr;
            }
        }
        if (auto i = ranges::find(_ready, h); i != _ready.end())
            _ready.erase(i);
    }

    
#ifndef NDEBUG
    void Scheduler::finished(coro_handle h) {
        LSched->debug("finished {}", logCoro{h});
        precondition(isCurrent());
        assert(h.done());
        assert(!isReady(h));
        assert(!isWaiting(h));
    }
#endif


    /// Called from "normal" code.
    /// Resumes the next ready coroutine and returns true.
    /// If no coroutines are ready, returns false.
    bool Scheduler::resume() {
        if (coro_handle h = nextOr(nullptr)) {
            lifecycle::resume(h);
            return true;
        } else {
            return false;
        }
    }


    bool Scheduler::isReady(coro_handle h) const {
        return std::ranges::any_of(_ready, [=](auto x) {return x == h;});
    }

    bool Scheduler::isWaiting(coro_handle h) const {
        return _suspended->find(h.address()) != _suspended->end();
    }

    /// Changes a waiting coroutine's state to 'ready' and notifies the Scheduler to resume
    /// if it's blocked in next(). At some point next() will return this coroutine.
    /// \note  This method is thread-safe.
    void Scheduler::wakeUp() {
        if (_woke.exchange(true) == false)
            _wakeUp();
    }

    bool Scheduler::hasWakers() const {
        if (_woke) {
            for (auto &[h, sus] :*_suspended) {
                if (sus._wakeMe.test() && sus._handle)
                    return true;
            }
        }
        return false;
    }

    // Finds any waiting coroutines that want to wake up, removes them from `_suspended`
    // and adds them to `_ready`.
    void Scheduler::scheduleWakers() {
        while (_woke.exchange(false) == true) {
            // Some waiting coroutine is now ready:
            for (auto i = _suspended->begin(); i != _suspended->end();) {
                if (i->second._wakeMe.test()) {
                    if (i->second._handle) {
                        LSched->debug("scheduleWaker({})", logCoro{i->second._handle});
                        _ready.push_back(i->second._handle);
                    } else {
                        LSched->debug("cleaned up canceled Suspension {}", (void*)&i->second);
                    }
                    i = _suspended->erase(i);
                } else {
                    ++i;
                }
            }
        }
    }

}
