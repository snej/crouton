//
// Result.hh
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
#include "crouton/util/Base.hh"
#include "crouton/Error.hh"

#include <variant>

namespace crouton {

    /** A `Result<T>` is either empty, or holds a value of type `T`, or an `Error`.
        (A `Result<void>` has no explicit value, but still distinguishes between empty/nonempty.)
        It's commonly used as a return type, and in the implementation of `Future<T>`. */
    template <typename T>
    class [[nodiscard("check Result for error")]] Result {
    public:
        using TT = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

        Result() noexcept               :_value(noerror) { }
        Result(Error err) noexcept      :_value(err) { }

        template <typename U> requires (!std::is_void_v<T> && std::constructible_from<T, U>
                                            && !std::same_as<std::remove_reference_t<U>, Result<T>>)
        Result(U&& val)                 :_value(std::forward<U>(val)) { }

        template <typename U> requires (!std::is_void_v<T> && std::constructible_from<T, U>
                                            && !std::same_as<std::remove_reference_t<U>, Result<T>>)
        Result& operator=(U&& val)      {set(std::forward<U>(val)); return *this;}

        Result& operator=(Error err)    {_value = err; return *this;}

        Result(Result&& r) noexcept             = default;
        Result(Result const& r)                 = default;
        Result& operator=(Result const& r)      = default;
        Result& operator=(Result&& r) noexcept  = default;

        /// Stores a value (same as `operator=`)
        template <typename U> requires (!std::is_void_v<T> && std::constructible_from<T, U>)
        void set(U&& val)                           {_value = std::forward<U>(val);}

        /// Stores an (empty) value in a `Result<void>`.
        void set() requires (std::is_void_v<T>)     {_value = TT();}

        /// True if there is a T value.
        bool ok() const noexcept Pure               {return _value.index() == 0;}

        /// True if there is neither a value nor an error.
        bool empty() const noexcept Pure {
            Error const* err = std::get_if<Error>(&_value);
            return err && !*err;
        }

        /// True if there is an error.
        bool isError() const noexcept Pure {
            Error const* err = std::get_if<Error>(&_value);
            return err && *err;
        }

        /// True if there's a value, false if empty. If there's an error, throws it.
        /// @throws Exception  if the Result holds an error.
        explicit operator bool() const {
            if (Error const* err = std::get_if<Error>(&_value)) [[unlikely]] {
                if (*err) [[unlikely]]
                    err->raise();
                return false;
            } else {
                return true;
            }
        }

        /// Returns the error, if any, else `noerror`.
        Error error() const noexcept Pure {
            Error const* err = std::get_if<Error>(&_value);
            return err ? *err : noerror;
        }

        /// Returns the value, or throws the error as an exception.
        /// @throws Exception  if the Result holds an error.
        TT const& value() const &  requires (!std::is_void_v<T>) {
            raise_if();
            return std::get<T>(_value);
        }

        /// Returns the value, or throws the error as an exception.
        /// @throws Exception  if the Result holds an error.
        TT& value() &  requires (!std::is_void_v<T>) {
            raise_if();
            return std::get<T>(_value);
        }

        /// Returns the value, or throws the error as an exception.
        /// @throws Exception  if the Result holds an error.
        TT&& value() &&  requires (!std::is_void_v<T>) {
            raise_if();
            return std::get<T>(std::move(_value));
        }

        /// There's no value to return, but this checks for an error and if so throws it.
        /// @throws Exception  if the Result holds an error.
        void value() const &  requires (std::is_void_v<T>) {
            raise_if();
        }

        /// The `*` and `->` operators can be used to access the value.
        /// @note  Unlike `std::optional`, these _do_ check whether there's a value.
        /// @throws Exception  if the Result does not contain a value.
        TT& operator*()              requires (!std::is_void_v<T>)  {return value();}
        TT const& operator*() const  requires (!std::is_void_v<T>)  {return value();}
        TT* operator->()             requires (!std::is_void_v<T>)  {return &value();}

        /// Writes either the value or the error to the stream.
        friend ostream& operator<<(ostream& out, Result const& r) {
            if (r.ok()) {
                return out << std::get<T>(r._value);
            } else {
                return out << r.error();
            }
        }

    private:
        void raise_if() const {
            if (Error const* errp = std::get_if<Error>(&_value)) [[unlikely]] {
                Error err = *errp ? *errp : CroutonError::EmptyResult;
                err.raise();
            }
        }

        std::variant<TT,Error> _value;
    };



#if 0 //FIXME: `({...})` is a GCC extension, not standard C++, and MSVC doesn't support it :(
    /// Syntactic sugar to handle a `Result<T>`, similar to Swift's `try`.
    /// - If R has a value, evaluates to its value.
    /// - If R has an error, `co_return`s the error from the current coroutine.
    #define UNWRAP(R) \
        ({ auto _r_ = (R); \
           if (_r_.isError())  co_return _r_.error(); \
           std::move(_r_).value(); })

    /// Syntactic sugar to await a Future without throwing exceptions.
    /// - If the future returns a value, evaluates to that value.
    /// - If the future returns an error, `co_return`s the error.
    #define TRY_AWAIT(F)    UNWRAP(AWAIT NoThrow(F))
#endif
}
