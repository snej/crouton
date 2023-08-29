//
// Actor.hh
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
#include "Future.hh"
#include <deque>

namespace crouton {

    /** An Actor is a class that runs one Future-returning coroutine method at a time,
        even if those methods are called concurrently, even from multiple threads.
        It keeps a queue of waiting calls, and when one coroutine completes it starts the next. */
    class Actor {
    public:
        /// By default, an Actor's coroutines run on the thread it was created on.
        Actor()                                     :Actor(Scheduler::current()) { }

        /// You can explicitly associate an Actor with a particular thread's Scheduler.
        explicit Actor(Scheduler& sched)            :_scheduler(sched) { }

        ~Actor()                                    {assert(!_activeCoro); assert(_queue.empty());}

        Scheduler& scheduler()                      {return _scheduler;}

    private:
        template <typename T> friend class ActorMethodImpl;

        // Called when a new coroutine method is invoked.
        // If on the right thread and I'm not running anything, start the coroutine right away.
        // Else queue it.
        bool startNew(coro_handle h) const {
            if (_scheduler.isCurrent()) {
                if (_activeCoro == nullptr) {
                    std::cerr << "Actor " << (void*)this << " immediately starting " << h << std::endl;
                    _activeCoro = h;
                    return true;
                } else {
                    std::cerr << "Actor " << (void*)this << " queued " << h << std::endl;
                    _queue.push_back(h);
                }
            } else {
                _scheduler.onEventLoop([this,h]{
                    if (_activeCoro == nullptr) {
                        _activeCoro = h;
                        std::cerr << "Actor " << (void*)this << " scheduled " << h << std::endl;
                        _scheduler.schedule(h);
                    } else {
                        std::cerr << "Actor " << (void*)this << " queued " << h << std::endl;
                        _queue.push_back(h);
                    }
                });
            }
            return false;
        }

        // Called when a coroutine method finishes. Schedules the next one, if any.
        void finished(coro_handle h) const {
            assert(_scheduler.isCurrent()); // this is always called on the scheduler's thread.
            assert(h == _activeCoro);
            if (_queue.empty()) {
                _activeCoro = nullptr;
            } else {
                _activeCoro = _queue.front();
                _queue.pop_front();
                std::cerr << "Actor " << (void*)this << " scheduled " << h << std::endl;
                _scheduler.schedule(_activeCoro);
            }
        }

        Scheduler&                      _scheduler;     // Scheduler of the thread my coros run on
        coro_handle mutable             _activeCoro;    // Currently active coroutine
        std::deque<coro_handle> mutable _queue;         // Queued coroutines
    };


    // Implementation of an Actor method returning a Future.
    template <typename T>
    class ActorMethodImpl : public FutureImpl<T> {
    public:

        template <typename... ArgTypes>
        ActorMethodImpl(crouton::Actor& actor, ArgTypes... args)
        :_actor(actor)
        {
            std::cerr << "Created ActorMethodImpl " << (void*)this << " on Actor " << (void*)&actor << std::endl;
        }

        template <typename... ArgTypes>
        ActorMethodImpl(crouton::Actor const& actor, ArgTypes... args)
        :_actor(actor)
        {
            std::cerr << "Created ActorMethodImpl " << (void*)this << " on const Actor " << (void*)&actor << std::endl;
        }

        struct suspendInitial : public CORO_NS::suspend_always {
            ActorMethodImpl* _impl;
            bool await_ready() const noexcept {
                return _impl->_actor.startNew(_impl->handle());
            }
        };

        struct suspendFinal : public Finisher {
            ActorMethodImpl* _impl;
            coro_handle await_suspend(coro_handle h) noexcept {
                _impl->_actor.finished(h);
                return Finisher::await_suspend(h);
            }
        };

        suspendInitial initial_suspend()        { return suspendInitial{{},this}; }
        suspendFinal final_suspend() noexcept   { return suspendFinal{{},this}; }

    private:
        ActorMethodImpl() = delete;
        
        Actor const& _actor;
    };

}


// Magic glue that specifies that methods of an Actor subclass which return Future will be
// implemented with ActorMethodImpl.
template <typename T, typename ACTOR, typename... ArgTypes>
    requires(is_type_complete_v<ACTOR> &&
             std::derived_from<ACTOR, crouton::Actor>)
struct CORO_NS::coroutine_traits<crouton::Future<T>, ACTOR&, ArgTypes...> {
    using promise_type = crouton::ActorMethodImpl<T>;
};

// same as above, but for a const Actor method
template <typename T, typename ACTOR, typename... ArgTypes>
    requires(is_type_complete_v<ACTOR> &&
             std::derived_from<ACTOR, crouton::Actor>)
struct CORO_NS::coroutine_traits<crouton::Future<T>, ACTOR const&, ArgTypes...> {
    using promise_type = crouton::ActorMethodImpl<T>;
};
