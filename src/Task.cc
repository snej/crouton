//
// Task.cc
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

#include "crouton/Task.hh"
#include "crouton/util/Logging.hh"

namespace crouton {

    
    Task TaskImpl::get_return_object() {
        _shared = std::make_shared<shared>();
        return Task(typedHandle(), _shared);
    }


    void TaskImpl::unhandled_exception() {
        LCoro->info("Task {} exiting with exception", minifmt::write{logCoro{handle()}});
        lifecycle::threw(_handle);
        _shared->alive = false;
        _shared->done.notify(Error(std::current_exception()));
    }


    void TaskImpl::return_void() {
        LCoro->info("Task {} finished", minifmt::write{logCoro{handle(), true}});
        lifecycle::returning(handle());
        _shared->alive = false;
        _shared->done.notify(noerror);
    }


}
