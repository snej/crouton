//
// Queue.hh
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
#include "crouton/CoCondition.hh"
#include "crouton/Error.hh"
#include "crouton/Future.hh"
#include "crouton/Generator.hh"
#include "crouton/Result.hh"
#include "crouton/Task.hh"

#include <deque>
#include <optional>

namespace crouton {


    /** A producer-consumer queue that provides a Generator for reading items asynchronously. */
    template <typename T>
    class AsyncQueue {
    public:
        AsyncQueue() = default;
        AsyncQueue(AsyncQueue&&) noexcept = default;
        AsyncQueue& operator=(AsyncQueue&&) noexcept = default;

        virtual ~AsyncQueue() {close();}    // warning: will not call overridden `close` methods!

        enum State : uint8_t {Open, Closing, Closed};

        /// The current state: Open (items can be pushed & popped), Closing (no more pushes),
        /// Closed (no more pops.)
        State state() const                         {return _state;}

        /// Closes the push/input end of the queue. The State changes to Closing.
        /// No more items can be pushed (the methods return false), but items can be popped.
        /// The Generator will keep returning items until the queue empties, then set the state to
        /// Closed and signal EOF by returning `noerror` (or the optional Error parameter.)
        virtual void closePush(Error err = noerror) {
            if (_state == Open) {
                _state = Closing;
                _closeWhenEmpty = true;
                if (!_closeError)
                    _closeError = err;
            }
        }

        /// After this is called, when the queue becomes empty it will close.
        /// @note  The difference between this and `closePush` is that items may still be pushed.
        void closeWhenEmpty(Error err = noerror) {
            if (_queue.empty())
                close(err);
            else {
                _closeWhenEmpty = true;
                if (!_closeError)
                    _closeError = err;
            }
        }

        /// Closes the queue immediately: removes all its items and sets its state to Closed.
        /// Nothing can be pushed or popped. The Generator will immediately signal EOF by returning
        /// `noerror` (or the optional Error parameter.)
        virtual void close(Error err = noerror) {
            if (_state != Closed) {
                _state = Closed;
                if (!_closeError)
                    _closeError = err;
                _queue.clear();
                _pullCond.notifyOne();
            }
        }

        bool empty() const                          {return _queue.empty();}
        size_t size() const                         {return _queue.size();}
        Error error() const                         {return empty() ? _closeError : noerror;}

        using iterator = typename std::deque<T>::iterator;
        using const_iterator = typename std::deque<T>::const_iterator;

        iterator begin()                            {return _queue.begin();}
        iterator end()                              {return _queue.end();}
        const_iterator begin() const                {return _queue.begin();}
        const_iterator end() const                  {return _queue.end();}

        /// True if the queue contains an item equal to `item`.
        bool contains(T const& item) const          {return find(item) != _queue.end();}

        /// Returns an iterator to the first item equal to `item`.
        const_iterator find(T const& item) const    {return std::find(begin(), end(), item);}

        /// Returns a pointer to the first item for which the predicate returns true, else null.
        template <typename PRED>
        T const* find_if(PRED pred) const {
            if (auto i = std::find_if(begin(), end(), pred); i != end())
                return &*i;
            return nullptr;
        }

        /// Adds an item at the tail of the queue.
        virtual bool push(T t) {
            if (_state != Open)
                return false;
            _queue.emplace_back(std::move(t));
            if (_queue.size() == 1)
                _pullCond.notifyOne();
            return true;
        }

        /// Adds an item at the position of the iterator, i.e. before whatever's at the iterator.
        virtual bool pushBefore(typename std::deque<T>::iterator i, T item) {
            if (_state != Open)
                return false;
            _queue.emplace(i, std::move(item));
            if (_queue.size() == 1)
                _pullCond.notifyOne();
            return true;
        }

        bool push(Result<T> r) {
            if (r.ok())
                return push(r.value());
            else
                return closePush(r.error()), true;
        }

        /// Returns a pointer to the front item (the one that would be popped), or null if empty.
        T const* peek() const {
            return empty() ? nullptr : &_queue.front();
        }

        /// Removes and returns the front item. It is illegal to call this when empty.
        virtual T pop() {
            assert(!empty());
            T item(std::move(_queue.front()));
            _queue.pop_front();
            if (_closeWhenEmpty && _queue.empty())
                close();
            return item;
        }

        /// Removes and returns the front item. Returns `nullopt` if empty.
        std::optional<T> maybePop() {
            if (!empty())
                return pop();
            return std::nullopt;
        }

        /// Removes an item equal to the given `item`.
        virtual bool remove(T const& item) {
            precondition(_state == Open);
            if (auto i = find(item); i != _queue.end()) {
                _queue.erase(i);
                return true;
            }
            return false;
        }

        //---- Asynchronous API:

        /// Returns a Generator that will yield items from the queue until it closes.
        /// @warning  Currently, this can only be called once.
        Generator<T> generate() {
            precondition(!_generating);
            _generating = true;
            while (_state != Closed) {
                if (_queue.empty()) {
                    if (_closeWhenEmpty) {
                        close();
                        break;
                    }
                    AWAIT _pullCond;
                    if (_queue.empty())
                        break;
                }
                T item = pop();
                YIELD std::move(item);
            }
            if (_closeError)
                YIELD _closeError;
        }

    protected:
        std::deque<T> _queue;
        CoCondition   _pullCond;
        Error         _closeError;
        State         _state = Open;
        bool          _generating = false;
        bool          _closeWhenEmpty = false;
    };



    /** A subclass of AsyncQueue that has a maximum size.
        Push operations will return false if they would exceed the maximum.
        An `asyncPush` method waits until there's room before returning. */
    template <typename T>
    class BoundedAsyncQueue : public AsyncQueue<T> {
    public:
        using super = AsyncQueue<T>;

        explicit BoundedAsyncQueue(size_t maxSize) :_maxSize(maxSize) {precondition(maxSize > 0); }

        /// True if the queue is at capacity and no items can be pushed.
        bool full() const   {return this->size() >= _maxSize;}

        //---- Asynchronous API:

        /// Pushes an item; if the queue is full, waits for there to be room.
        /// @returns  true if pushed, false if the queue closed.
        ASYNC<bool> asyncPush(T t) {
            while (full() && this->state() == super::Open)
                AWAIT _pushCond;
            RETURN push(std::move(t));
        }

        ASYNC<bool> asyncPush(Result<T> r) {
            if (r.ok())
                return asyncPush(r.value());
            else
                return closePush(r.error()), Future<bool>(true);
        }

        /// Starts a coroutine that awaits values from the Generator and pushes them to the queue.
        /// When the Generator ends, it calls `closeWhenEmpty`.
        Task pushGenerator(Generator<T> gen) {
            while (this->state() == super::Open) {
                Result<T> result = AWAIT gen;
                if (result.ok()) {
                    if (! AWAIT this->asyncPush(std::move(result).value()))
                        break;
                } else {
                    this->closeWhenEmpty();
                    break;
                }
                if (bool ok = YIELD 0; !ok)
                    break;
            }
        }

        //---- Overrides:

        void closePush(Error err = noerror) override {
            super::closePush(err);
            _pushCond.notifyAll();
        }

        void close(Error err = noerror) override {
            super::close(err);
            _pushCond.notifyAll();
        }

        [[nodiscard]] bool push(T t) override {
            return !full() && super::push(std::move(t));
        }

        [[nodiscard]] bool pushBefore(typename std::deque<T>::iterator i, T item) override {
            return !full() && super::pushBefore(i, std::move(item));
        }

        T pop() override {
            if (full()) {
                T result(super::pop());
                _pushCond.notifyOne();
                return result;
            } else {
                return super::pop();
            }
        }

        bool remove(T const& item) override {
            bool wasFull = full();
            bool removed = super::remove(item);
            if (wasFull && removed)
                _pushCond.notifyOne();
            return removed;
        }

    private:
        size_t        _maxSize;
        CoCondition   _pushCond;
    };

}
