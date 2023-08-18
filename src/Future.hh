//
// Future.hh
//
// Copyright © 2023 Jens Alfke. All rights reserved.
//

#pragma once
#include "Coroutine.hh"
#include "Scheduler.hh"
#include <cassert>
#include <exception>

namespace snej::coro {
    template <typename T> class Future;
    template <typename T> class FutureImpl;

    template <typename T> struct ref { using type = T&; };
    template <> struct ref<void> {using type = void; };


    class FutureStateBase {
    public:
        bool hasValue() const;
        std::coroutine_handle<> suspend(std::coroutine_handle<> coro);
        void setException(std::exception_ptr x);

    protected:
        void _gotValue();
        void _checkValue();

        mutable std::mutex  _mutex;
        Suspension*         _suspension = nullptr;
        std::exception_ptr  _exception;
        bool                _hasValue = false;
    };


    template <typename T>
    class FutureState : public FutureStateBase {
    public:
        T& value() {
            std::unique_lock<std::mutex> lock(_mutex);
            _checkValue();
            assert(_value);
            return *_value;
        }

        void setValue(T&& value) {
            std::unique_lock<std::mutex> lock(_mutex);
            _value = std::forward<T>(value);
            _gotValue();
        }

    private:
        std::optional<T> _value {};
    };

    template <>
    class FutureState<void> : public FutureStateBase {
    public:
        void value();
        void setValue();
    };



    /// The producer side of a Future, which is responsible for setting its value.
    template <typename T>
    class FutureProvider {
    public:
        /// Constructs a Future that doesn't have a value yet.
        FutureProvider()                        :_state(std::make_shared<FutureState<T>>()) { }

        /// Creates a Future that can be returned to callers.
        Future<T> future()                      {return Future<T>(_state);}
        operator Future<T>()                    {return future();}

        /// True if there is a value.
        bool hasValue() const                   {return _state->hasValue();}

        /// Sets the Future's value and unblocks anyone waiting for it.
        void setValue(T&& t) const              {_state->setValue(std::forward<T>(t));}

        /// Sets the Future's result as an exception and unblocks anyone waiting for it.
        /// Calling value() will re-throw the exception.
        void setException(std::exception_ptr x) {_state->setException(x);}

        /// Gets the future's value, or throws its exception.
        /// It's illegal to call this before a value is set.
        T& value() const                        {return _state->value();}

    private:
        std::shared_ptr<FutureState<T>> _state;
    };

    template <>
    class FutureProvider<void> {
    public:
        FutureProvider()                        :_state(std::make_shared<FutureState<void>>()) { }
        Future<void> future();
        operator Future<void>();
        bool hasValue() const                   {return _state->hasValue();}
        void setValue() const                   {_state->setValue();}
        void setException(std::exception_ptr x) {_state->setException(x);}
        void value() const                      {return _state->value();}
    private:
        std::shared_ptr<FutureState<void>> _state;
    };



    /// Represents a value, produced by a `FutureProvider<T>`, that may not be available yet.
    /// A coroutine can get the value by calling `co_await` on it, which suspends the coroutine
    /// until the value is available.
    template <typename T>
    class Future : public CoroutineHandle<FutureImpl<T>> {
    public:
        bool hasValue() const           {return _state->hasValue();}

        /// Blocks until the value is available. Must NOT be called from a coroutine!
        /// Requires that this Future be returned from a coroutine.
        ref<T>::type waitForValue()     {return this->handle().promise().waitForValue();}

        // These methods make Future awaitable:
        bool await_ready()              {return _state->hasValue();}
        ref<T>::type await_resume()     {return _state->value();}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coro) noexcept{
            return _state->suspend(coro);
        }

    private:
        friend class FutureProvider<T>;
        friend class FutureImpl<T>;

        Future(std::shared_ptr<FutureState<T>> state)   :_state(std::move(state)) { }

        std::shared_ptr<FutureState<T>>  _state;
    };


#pragma mark - FUTURE IMPL:


    // Implementation of a coroutine that returns a Future<T>.
    template <typename T>
    class FutureImpl : public CoroutineImpl<Future<T>,FutureImpl<T>> {
    public:
        using super = CoroutineImpl<Future<T>,FutureImpl<T>>;
        using handle_type = super::handle_type;

        FutureImpl() = default;

        T& waitForValue() {
            while (!_provider.hasValue())
                handle().resume();
            return _provider.value();
        }

        //---- C++ coroutine internal API:

        Future<T> get_return_object() {
            auto f = _provider.future();
            f.setHandle(handle());
            return f;
        }

        std::suspend_never initial_suspend()    {return {};}
        void unhandled_exception()              {_provider.setException(std::current_exception());}
        void return_value(T&& value)            {_provider.setValue(std::forward<T>(value));}

    private:
        handle_type handle()                    {return handle_type::from_promise(*this);}
        FutureProvider<T> _provider;
    };


    template <>
    class FutureImpl<void> : public CoroutineImpl<Future<void>,FutureImpl<void>> {
    public:
        using super = CoroutineImpl<Future<void>,FutureImpl<void>>;
        using handle_type = super::handle_type;
        FutureImpl() = default;
        void waitForValue();
        Future<void> get_return_object();
        std::suspend_never initial_suspend();
        void unhandled_exception();
        void return_void();
    private:
        handle_type handle();
        FutureProvider<void> _provider;
    };

}
