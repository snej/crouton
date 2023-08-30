//
// Task.hh
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
#include "Scheduler.hh"

namespace crouton {
    class TaskImpl;


    /** Return type for a coroutine that doesn't return a value, just runs indefinitely.
        Unlike the base class CoroutineHandle, it does not destroy the coroutine handle in its
        destructor. */
    class Task : public CoroutineHandle<TaskImpl> {
    public:
        ~Task()     {setHandle(nullptr);}   // don't let parent destructor destroy the coroutine
        Task(Task&&) = default;
    protected:
        friend class Scheduler;
    private:
        friend class TaskImpl;
        explicit Task(handle_type h) :CoroutineHandle<TaskImpl>(h) { }
    };



    class TaskImpl : public CoroutineImpl<Task, TaskImpl> {
    public:
        ~TaskImpl() = default;
        Task get_return_object()                {return Task(handle());}
        CORO_NS::suspend_never initial_suspend() {
            //std::cerr << "New " << typeid(this).name() << " " << handle() << std::endl;
            return {};
        }
        Yielder yield_value(bool)               { return Yielder(handle()); }
        void return_void()                      { }
    private:
    };
}