//
// Future.hh
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
#include "crouton/Awaitable.hh"
#include "crouton/Coroutine.hh"
#include "crouton/CroutonFwd.hh"
#include "crouton/Result.hh"
#include "crouton/Scheduler.hh"
#include "crouton/Select.hh"

#include <atomic>
#include <exception>
#include <functional>

namespace crouton {
    template <typename T> class FutureImpl;
    template <typename T> class FutureState;
    template <typename T> class NoThrow;

    template <typename T> using FutureProvider = std::shared_ptr<FutureState<T>>;


    /** Represents a value of type `T` that may not be available yet.

        This is a typical type for a coroutine to return. The coroutine function can just
        `co_return` a value of type `T`, or an `Error` to indicate failure,
        or even throw an exception.

        A coroutine that gets a Future from a function call should `co_await` it to get its value.
        Or if the Future resolves to an exception, the `co_await` will re-throw it.

        A regular function can return a Future by creating a `FutureProvider` local variable and
        returning it, which implicitly creates a `Future` from it. It needs to arrange, via a
        callback or another thread, to call `setResult` or `setError` on the provider; this
        resolves the future and unblocks anyone waiting. If the function finds it can return a
        value immediately, it can just return a Future constructed with its value (or exception.)

        A regular function that gets a Future can call `then()` to register a callback. */
    template <typename T>
    class [[nodiscard("Future must be AWAITed or returned")]]
    Future : public Coroutine<FutureImpl<T>>, public ISelectable {
    public:
        using nonvoidT = std::conditional_t<std::is_void_v<T>, std::byte, T>;

        /// Creates a FutureProvider, with which you can create a Future and later set its value.
        static FutureProvider<T> provider()             {return std::make_shared<FutureState<T>>();}

        /// Creates a Future from a FutureProvider.
        explicit Future(FutureProvider<T> state)        :_state(std::move(state)) {assert(_state);}

        /// Creates an already-ready `Future`.
        /// @note In `Future<void>`, this constructor takes no parameters.
        Future(nonvoidT&& v)  requires (!std::is_void_v<T>) {_state->setResult(std::move(v));}

        /// Creates an already-ready `Future`.
        /// @note In `Future<void>`, this constructor takes no parameters.
        Future(nonvoidT const& v)  requires (!std::is_void_v<T>) {_state->setResult(v);}

        /// Creates an already-ready `Future<void>`.
        Future()  requires (std::is_void_v<T>)          {_state->setResult();}

        /// Creates an already-failed future :(
        Future(Error err)                               {_state->setResult(err);}
        Future(ErrorDomain auto d)                      :Future(Error(d)) { }

        Future(Future&&) = default;
        ~Future()                                       {if (_state) _state->noFuture();}

        /// True if a value or error has been set by the provider.
        bool hasResult() const                          {return _state->hasResult();}

        /// Returns the result, or throws the exception. Don't call this if hasResult is false.
        std::add_rvalue_reference_t<T> result() const   {return _state->resultValue();}

        /// Returns the error, if any, else noerror. Don't call this if hasResult is false.
        Error error() const                             {return _state->getError();}

        /// Registers a callback that will be called when the result is available, and which can
        /// return a new value (or void) which becomes the result of the returned Future.
        /// @param fn A callback that will be called when the value is available.
        /// @returns  A new Future whose result will be the return value of the callback,
        ///           or a `Future<void>` if the callback doesn't return a value.
        /// @note  If this Future already has a result, the callback is called immediately,
        ///        before `then` returns, and thus the returned Future will also have a result.
        /// @note  If this Future fails with an exception, the callback will not be called.
        ///        Instead the returned Future's result will be the same exception.
        /// @note  The callback will run on the same thread that is calling `then`. If the Future
        ///        is assigned a result on a different thread, the callback is scheduled via
        ///        Scheduler::asap, so the original thread's event loop needs to be active.
        template <typename FN, typename U = std::invoke_result_t<FN,T>> requires(!std::is_void_v<T>)
        [[nodiscard]] Future<U> then(FN fn);

        template <typename FN, typename U = std::invoke_result_t<FN>> requires(std::is_void_v<T>)
        [[nodiscard]] Future<U> then(FN);

        /// From ISelectable interface.
        void onReady(OnReadyFn fn) override  {_state->onReady(std::move(fn));}

        //---- These methods make Future awaitable:
        bool await_ready() {
            return _state->hasResult();
        }
        auto await_suspend(coro_handle coro) noexcept {
            if (this->handle())
                return lifecycle::suspendingTo(coro, this->handle(), _state->suspend(coro));
            else
                return lifecycle::suspendingTo(coro, CRTN_TYPEID(*this), this, _state->suspend(coro));
        }
        [[nodiscard]] std::add_rvalue_reference_t<T> await_resume() requires (!std::is_void_v<T>) {
            return std::move(_state->resultValue());
        }

        void await_resume() requires (std::is_void_v<T>) {
            _state->resultValue();
        }

        //---- Synchronous/blocking accessors. Only for non-coroutine callers.

        /// Blocks (by running the event loop) until the Future completes,
        /// then returns its result or error as a `Result<T>`.
        [[nodiscard]] Result<T> wait() {
            Scheduler& sched = Scheduler::current();
            (void)sched.eventLoop(); // create it in advance
            (void) this->onReady([&]() {
                sched.asap([&] { });
            });
            sched.runUntil([&] {return hasResult();});
            return _state->result();
        }

        /// Blocks (by running the event loop) until the Future completes,
        /// then either returns its result or throws its error as an exception.
        T waitForResult() {
            return wait().value();
        }

    private:
        using super = Coroutine<FutureImpl<T>>;
        friend class FutureImpl<T>;
        friend class NoThrow<T>;

        Future(typename super::handle_type h, FutureProvider<T> state)
        :super(h)
        ,_state(std::move(state))
        {assert(_state);}

        FutureProvider<T> _state = std::make_shared<FutureState<T>>();
    };


#pragma mark - FUTURE STATE:


    // Internal base class of FutureState<T>.
    class FutureStateBase : public std::enable_shared_from_this<FutureStateBase> {
    public:
        bool hasResult() const                       {return _state.load() == Ready;}

        void onReady(ISelectable::OnReadyFn);   // Called by Future::onReady
        void noFuture();                        // Called by Future::~Future
        coro_handle suspend(coro_handle coro);  // Called by Future::await_suspend

        virtual void setError(Error) = 0;
        virtual Error getError() = 0;

        using ChainCallback = std::function<void(std::shared_ptr<FutureStateBase> dstState,
                                                 std::shared_ptr<FutureStateBase> srcState)>;

        template <typename U>
        Future<U> chain(ChainCallback fn) {
            auto provider = std::make_shared<FutureState<U>>();
            _chain(provider, fn);
            return Future<U>(std::move(provider));
        }

    protected:
        enum State : uint8_t {
            Empty,      // initial state
            Waiting,    // a coroutine is waiting and _suspension is set
            Chained,    // another Future is chained to this one with `then(...)`
            Ready       // result is available and _result is set
        };

        virtual ~FutureStateBase() = default;
        bool checkEmpty();
        bool changeState(State);
        void _notify();
        void _chain(std::shared_ptr<FutureStateBase>, ChainCallback);
        void resolveChain();

        Suspension                       _suspension;           // coro that's awaiting result
        std::shared_ptr<FutureStateBase> _chainedFuture;        // Future of a 'then' callback
        ChainCallback                    _chainedCallback;      // 'then' callback
        Scheduler*                       _chainedScheduler = nullptr; // Sched to run 'then' on
        ISelectable::OnReadyFn           _onReady;              // `onReady` callback
        std::atomic<bool>                _hasOnReady = false;
        std::atomic<State>               _state = Empty;        // Current state, for thread-safety
    };


    /** The actual state of a Future, also known as its provider.
        It's used to set the result/error. */
    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        template <typename U>
        void setResult(U&& value)  requires (!std::is_void_v<T>) {
            _result = std::forward<U>(value);
            assert(!_result.empty());   // A Future's result cannot be `noerror`
            _notify();
        }

        void setResult()  requires (std::is_void_v<T>) {
            _result.set();
            _notify();
        }
        void setResult(Error err)  requires (std::is_void_v<T>) {
            if (err)
                _result = err;
            else
                _result.set(); // set success
            _notify();
        }

        void setError(Error x) override                 {setResult(x);}
        Error getError() override                       {return _result.error();}

    private:
        friend class Future<T>;
        friend class NoThrow<T>;

        Result<T> && result() &&                        {return std::move(_result);}
        Result<T> & result() &                          {return _result;}

        std::add_rvalue_reference_t<T> resultValue()  requires (!std::is_void_v<T>) {
            return std::move(_result).value();
        }

        void resultValue()  requires (std::is_void_v<T>) {
            _result.value();
        }

        Result<T> _result;
    };



    /** Wrap this around a Future before co_await'ing it, to get the value as a Result.
        This will not throw; instead, you have to check the Result for an error. */
    template <typename T>
    class NoThrow {
    public:
        NoThrow(Future<T>&& future)     :_handle(future.handle()), _state(std::move(future._state)) { }

        bool hasResult() const          {return _state->hasResult();}
        Result<T> const& result() &     {return _state->result();}
        Result<T> result() &&           {return std::move(_state)->result();}

        bool await_ready() noexcept     {return _state->hasResult();}
        coro_handle await_suspend(coro_handle coro) noexcept {
            return lifecycle::suspendingTo(coro, _handle, _state->suspend(coro));
        }
        [[nodiscard]] Result<T> await_resume() noexcept {
            Result<T> result(std::move(_state)->result());
            _state = nullptr;
            return result;
        }

    protected:
        coro_handle        _handle;
        FutureProvider<T>  _state;
    };


    /** Wrap this around a Future to turn it into a Series, so you can connect a Subscriber.
        The Series will, of course, only return at most a single value before EOF. */
    template <typename T>
    class FutureSeries : public NoThrow<T>, public ISeries<T> {
    public:
        using NoThrow<T>::NoThrow;

        bool await_ready() noexcept override {
            return !this->_state || NoThrow<T>::await_ready();
        }
        coro_handle await_suspend(coro_handle coro) noexcept override {
            return NoThrow<T>::await_suspend();
        }
        [[nodiscard]] Result<T> await_resume() noexcept override {
            if (this->_state)
                return NoThrow<T>::await_resume();
            else
                return Result<T>{};
        }
        virtual void onReady(ISelectable::OnReadyFn fn) override {
            if (this->_state)
                this->_state->onReady(std::move(fn));
            else
                fn();
        }
    };


#pragma mark - FUTURE IMPL:


    // Implementation (promise_type) of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<FutureImpl<T>, true> {
    public:
        using super = CoroutineImpl<FutureImpl<T>, true>;
        using handle_type = typename super::handle_type;
        using nonvoidT = std::conditional_t<std::is_void_v<T>, std::byte, T>;

        FutureImpl() = default;

        //---- C++ coroutine internal API:

        Future<T> get_return_object() {
            return Future<T>(this->typedHandle(), _provider);
        }

        void unhandled_exception() {
            this->super::unhandled_exception();
            _provider->setResult(Error(std::current_exception()));
        }

        void return_value(Error err) {
            lifecycle::returning(this->handle());
            _provider->setResult(err);
        }

        void return_value(ErrorDomain auto errVal) {
            return_value(Error(errVal));
        }

        void return_value(Result<T> result) {
            lifecycle::returning(this->handle());
            _provider->setResult(std::move(result));
        }

        void return_value(nonvoidT&& value)  requires (!std::is_void_v<T>) {
            lifecycle::returning(this->handle());
            _provider->setResult(std::move(value));
        }

        void return_value(nonvoidT const& value)  requires (!std::is_void_v<T>) {
            lifecycle::returning(this->handle());
            _provider->setResult(value);
        }

    protected:
        FutureProvider<T> _provider = Future<T>::provider();
    };



    // Future<T>::then, for T != void
    template <typename T>
    template <typename FN, typename U>  requires(!std::is_void_v<T>)
    Future<U> Future<T>::then(FN fn) {
        return _state->template chain<U>([fn](std::shared_ptr<FutureStateBase> const& baseState,
                                              std::shared_ptr<FutureStateBase> const& myBaseState) mutable {
            auto& state = static_cast<FutureState<U>&>(*baseState);
            T&& result = static_cast<FutureState<T>&>(*myBaseState).resultValue();
            if constexpr (std::is_void_v<U>) {
                fn(std::move(result));                      // <-- call fn
                state.setResult();
            } else {
                state.setResult(fn(std::move(result)));     // <-- call fn
            }
        });
    }

    // Future<T>::then, for T == void
    template <typename T>
    template <typename FN, typename U>  requires(std::is_void_v<T>)
    Future<U> Future<T>::then(FN fn) {
        return _state->template chain<U>([fn](std::shared_ptr<FutureStateBase> const& baseState,
                                              std::shared_ptr<FutureStateBase> const& myBaseState) mutable {
            auto& state = static_cast<FutureState<U>&>(*baseState);
            if constexpr (std::is_void_v<U>) {
                fn();                                       // <-- call fn
                state.setResult();
            } else {
                state.setResult(fn());                      // <-- call fn
            }
        });
    }

}
