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
#include "Coroutine.hh"
#include "Result.hh"
#include "Scheduler.hh"
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <cassert>

namespace crouton {
    template <typename T> class FutureImpl;
    template <typename T> class FutureProvider;
    template <typename T> class FutureState;

    /** Represents a value of type `T` that may not be available yet.

        This is a typical type for a coroutine to return. The coroutine function can just
        `co_return` a value of type `T`, or return a `std::exception` subclass to indicate failure,
        or even throw an exception.

        A coroutine that gets a Future from a function call should `co_await` it to get its value.
        Or if the Future resolves to an exception, the `co_await` will re-throw it.

        A regular function can return a Future by creating a `FutureProvider` local variable and
        returning it, which implicitly creates a `Future` from it. It needs to arrange, via a
        callback or another thread, to call `setResult` or `setException` on the provider; this
        resolves the future and unblocks anyone waiting. If the function finds it can return a
        value immediately, it can just return a Future constructed with its value (or exception.)

        A regular function that gets a Future can call `waitForValue()`, but this blocks the
        current thread. */
    template <typename T = void>
    class Future : public Coroutine<FutureImpl<T>> {
    public:
        /// Creates a Future from a FutureProvider.
        Future(FutureProvider<T> &provider)   :_state(provider._state) { }

        /// Creates an already-ready Future.
        /// @note In `Future<void>`, this constructor takes no parameters.
        Future(T&& value)
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setResult(std::move(value));
        }

        /// Creates an already-failed future :(
        template <class X>
        Future(X&& x) requires std::derived_from<X, std::exception>
        :_state(std::make_shared<FutureState<T>>()) {
            _state->setResult(std::forward<X>(x));
        }

        /// True if a value or exception has been set by the provider.
        bool hasValue() const           {return _state->hasValue();}

        /// Returns the value, or throws the exception. Don't call this if hasValue is false.
        T&& value() const               {return _state->value();}

        /// Blocks until the value is available. 
        /// @warning Do not call this in a coroutine! Use `co_await` instead.
        /// @warning Current implementation requires the Future have been returned from a coroutine.
        [[nodiscard]] T&& waitForValue() {return std::move(this->handle().promise().waitForValue());}

        // These methods make Future awaitable:
        bool await_ready()              {return _state->hasValue();}
        [[nodiscard]] T&& await_resume(){return std::move(_state->value());}
        auto await_suspend(coro_handle coro) noexcept {return _state->suspend(coro);}

    private:
        friend class FutureProvider<T>;
        friend class FutureImpl<T>;

        std::shared_ptr<FutureState<T>>  _state;
    };



    /** The producer side of a Future, which creates the Future object and is responsible for
        setting its value. Use this if you want to create a Future without being a coroutine. */
    template <typename T = void>
    class FutureProvider {
    public:
        /// Constructs a FutureProvider that doesn't have a value yet.
        FutureProvider()                        {reset();}

        /// Creates a Future that can be returned to callers. (Only call this once.)
        Future<T> future()                      {return Future<T>(*this);}

        /// True if there is a value.
        bool hasValue() const                   {return _state->hasValue();}

        /// Sets the Future's value, or exception, and unblocks anyone waiting for it.
        template <class X>
            void setResult(X&& x) const          {_state->setResult(std::forward<X>(x));}

        /// Gets the future's value, or throws its exception.
        /// It's illegal to call this before a value is set.
        T&& value() const                       {return std::move(_state->value());}

        /// Clears the provider, detaching it from its current Future, so it can create another.
        void reset()                            {_state = std::make_shared<FutureState<T>>();}
    private:
        friend class Future<T>;
        std::shared_ptr<FutureState<T>> _state;
    };



#pragma mark - INTERNALS:


    // Internal base class of the shared state co-owned by a Future and its FutureProvider.
    class FutureStateBase {
    public:
        bool hasValue() const;
        coro_handle suspend(coro_handle coro);

    protected:
        void _gotValue();

        mutable std::mutex  _mutex;                 // For thread-safety
        Suspension*         _suspension = nullptr;  // coro blocked awaiting my Future
        bool                _hasValue = false;      // True when a value or exception is set
    };


    // Internal state shared by a Future and its FutureProvider
    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        T&& value() {
            std::unique_lock<std::mutex> lock(_mutex);
            return std::move(_result).value();
        }

        template <typename U>
        void setResult(U&& value) {
            std::unique_lock<std::mutex> lock(_mutex);
            _result = std::forward<U>(value);
            _gotValue();
        }

    private:
        Result<T> _result;
    };


    template <>
    class FutureState<void> : public FutureStateBase {
    public:
        void value() {
            std::unique_lock<std::mutex> lock(_mutex);
            return _result.value();
        }
        void setResult() {
            std::unique_lock<std::mutex> lock(_mutex);
            _result.set();
            _gotValue();
        }
        template <typename U>
        void setResult(U&& value) {
            std::unique_lock<std::mutex> lock(_mutex);
            _result = std::forward<U>(value);
            _gotValue();
        }
    private:
        Result<void> _result;
    };


    template <>
    class FutureProvider<void> {
    public:
        FutureProvider()                        {reset();}
        Future<void> future();
        bool hasValue() const                   {return _state->hasValue();}
        void setResult() const                   {_state->setResult();}
        template <typename U>
            void setResult(U&& value) const      {_state->setResult(std::forward<U>(value));}
        void value() const                      {return _state->value();}
        void reset()                            {_state = std::make_shared<FutureState<void>>();}
    private:
        friend class Future<void>;
        std::shared_ptr<FutureState<void>> _state;
    };


    template <>
    class Future<void> : public Coroutine<FutureImpl<void>> {
    public:
        Future()                :_state(std::make_shared<FutureState<void>>()) {_state->setResult();}
        Future(FutureProvider<void> &p) :_state(p._state) { }
        template <class X>
        Future(X&& x) requires std::derived_from<X, std::exception>
        :_state(std::make_shared<FutureState<void>>()) {_state->setResult(std::forward<X>(x));}
        bool hasValue() const           {return _state->hasValue();}
        void value() const              {_state->value();}
        inline void waitForValue();
        bool await_ready()              {return _state->hasValue();}
        void await_resume()             {_state->value();}
        auto await_suspend(coro_handle coro) noexcept {return _state->suspend(coro);}
    protected:
        friend class FutureProvider<void>;
        friend class FutureImpl<void>;
        Future(std::shared_ptr<FutureState<void>> state)   :_state(std::move(state)) { }
        std::shared_ptr<FutureState<void>>  _state;
    };


#pragma mark - FUTURE IMPL:


    // Implementation (promise_type) of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<FutureImpl<T>> {
    public:
        using super = CoroutineImpl<FutureImpl<T>>;
        using handle_type = typename super::handle_type;

        FutureImpl() = default;

        T&& waitForValue() {
            Scheduler::current().runUntil([&]{ return _provider.hasValue(); });
            return std::move(_provider.value());
        }

        //---- C++ coroutine internal API:

        Future<T> get_return_object() {
            auto f = _provider.future();
            f.setHandle(this->handle());
            return f;
        }

        CORO_NS::suspend_never initial_suspend(){return {};}
        void unhandled_exception()              {_provider.setResult(std::current_exception());}
        template <class X>  // you can co_return an exception
        void return_value(X&& x) requires std::derived_from<X, std::exception> {
            _provider.setResult(std::forward<X>(x));
        }
        void return_value(T&& value)            {_provider.setResult(std::move(value));}
        void return_value(T const& value)       {_provider.setResult(value);}
        Finisher final_suspend() noexcept       {return {};}

    protected:
        FutureProvider<T> _provider;
    };


    template <>
    class FutureImpl<void> : public CoroutineImpl<FutureImpl<void>> {
    public:
        using super = CoroutineImpl<FutureImpl<void>>;
        using handle_type = super::handle_type;
        FutureImpl() = default;
        void waitForValue();
        Future<void> get_return_object();
        CORO_NS::suspend_never initial_suspend(){return {};}
        void unhandled_exception()              {_provider.setResult(std::current_exception());}
        void return_void()                      {_provider.setResult();}
        Finisher final_suspend() noexcept       {return {};}
    private:
        handle_type handle()                    {return handle_type::from_promise(*this);}
        FutureProvider<void> _provider;
    };


    void Future<void>::waitForValue()           {this->handle().promise().waitForValue();}

}
