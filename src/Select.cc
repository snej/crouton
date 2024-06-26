//
// Select.cc
//
// 
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

#include "crouton/Select.hh"
#include "crouton/Scheduler.hh"
#include "crouton/util/Logging.hh"

namespace crouton {

    Select::Select(std::initializer_list<ISelectable*> sources) {
        precondition(sources.size() <= kMaxSources);
        size_t i = 0;
        for (ISelectable* source : sources)
            _sources[i++] = source;
    }

    /// Begins watching the source at the given index.
    /// @note  Once a source has been returned from `co_await` it needs to be re-enabled
    ///        before it can be selected again.
    /// @note  If no sources are enabled, `co_await` will immediately return -1.
    void Select::enable(unsigned index) {
        precondition(_sources[index]);
        if (!_enabled[index]) {
            _enabled.set(index, true);
            _sources[index]->onReady([this,index]{this->notify(index);});
        }
    }

    Select& Select::enable() {
        for (unsigned i = 0; i < _sources.size() && _sources[i]; ++i)
            enable(i);
        return *this;
    }


    coro_handle Select::await_suspend(coro_handle h) {
        _suspension = Scheduler::current().suspend(h);
        return lifecycle::suspendingTo(h, CRTN_TYPEID(*this), this);
    }

    int Select::await_resume() {
        for (unsigned i = 0; i < kMaxSources; ++i) {
            if (_ready[i]) {
                _ready[i] = false;
                return i;
            }
        }
        Log->warn("Awaiting a non-enabled Select: will immediately return -1");
        return -1; // means "nothing was enabled"
    }

    Select::~Select() {
        for (unsigned i = 0; i < kMaxSources; ++i)
            if (_enabled[i])
                _sources[i]->onReady(nullptr);
    }

    void Select::notify(unsigned index) {
        _ready[index] = true;
        _enabled[index] = false;
        _suspension.wakeUp();
    }

}
