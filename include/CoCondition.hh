//
// CoCondition.hh
//
// 
//

#pragma once
#include "LinkedList.hh"
#include "Scheduler.hh"
#include <concepts>

namespace crouton {

    /** A cooperative condition variable. A coroutine that `co_await`s it will block until
        something calls `notify` or `notifyAll`. If there are multiple coroutines blocked,
        `notify` wakes one, while `notifyAll` wakes all of them.
        @warning  Not thread-safe, despite the name! */
    class CoCondition {
    public:
        struct awaiter;

        awaiter operator co_await() {return awaiter(this);}

        void notifyOne();

        void notifyAll();

        ~CoCondition() {assert(_awaiters.empty());}


        struct awaiter : public CORO_NS::suspend_always, private Link {
            awaiter(CoCondition* cond) :_cond(cond) { }
            ~awaiter();

            coro_handle await_suspend(coro_handle h) noexcept;

        private:
            friend class CoCondition;
            friend class LinkList;
            void wakeUp();

            CoCondition* _cond;
            Suspension*  _suspension = nullptr;
        };

    private:
        void remove(awaiter* a);

        LinkedList<awaiter> _awaiters;
    };


#pragma mark - BLOCKER:


    /** A simpler way to await a future event. A coroutine that `co_await`s a Blocker will block
        until something calls the Blocker's `notify` method. This provides an easy way to turn
        a completion-callback based API into a coroutine-based one: create a Blocker, start
        the operation, then `co_await` the Blocker. In the completion callback, call `notify`.

        The value you pass to `notify` will be returned from the `co_await`.

        Blocker supports only one waiting coroutine. If you need more, use a CoCondition. */
    template <typename T>
    class Blocker {
    public:
        bool await_ready() noexcept     {return _value.has_value();}

        coro_handle await_suspend(coro_handle h) noexcept {
            _suspension = Scheduler::current().suspend(h);
            return lifecycle::suspendingTo(h, typeid(*this), this);
        }

        T&& await_resume() noexcept     {return std::move(_value).value();}

        template <typename U>
        void notify(U&& val) {
            _value.emplace(std::forward<U>(val));
            if (auto s = _suspension) {
                _suspension = nullptr;
                s->wakeUp();
            }
        }

    private:
        std::optional<T> _value;
        Suspension* _suspension = nullptr;
    };


    template <>
    class Blocker<void> {
    public:
        bool await_ready() noexcept {return _hasValue;}

        coro_handle await_suspend(coro_handle h) noexcept {
            assert(!_suspension);
            _suspension = Scheduler::current().suspend(h);
            return lifecycle::suspendingTo(h, typeid(*this), this);
        }

        void await_resume() noexcept { }

        void notify() {
            _hasValue = true;
            if (auto s = _suspension) {
                _suspension = nullptr;
                s->wakeUp();
            }
        }

    private:
        Suspension* _suspension = nullptr;
        bool        _hasValue = false;
    };

}
