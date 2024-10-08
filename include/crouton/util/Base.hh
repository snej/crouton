//
// Base.hh
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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Find <coroutine> in std or experimental.
// Always use `CORO_NS` instead of `std` for coroutine types
#if defined(__has_include)
#   if __has_include(<coroutine>)
#       include <coroutine>
        namespace CORO_NS = std;
#   elif __has_include(<experimental/coroutine>)
#       include <experimental/coroutine>
        namespace CORO_NS = std::experimental
#   else
#      error No coroutine header
#   endif
#else
#   include <coroutine>
    namespace CORO_NS = std;
#endif


#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __unused
#if __has_attribute(unused)
#define __unused    __attribute__((unused))
#else
#define __unused
#endif
#endif

#if __has_attribute(__pure__)
#define Pure        __attribute__((__pure__))
#else
#define Pure
#endif

#if __cpp_rtti
#   define CROUTON_RTTI 1
#else
#   define CROUTON_RTTI 0
#endif

#if CROUTON_RTTI
#  define CRTN_TYPEID(T)  typeid(T)
#else
#  define CRTN_TYPEID(T)  (*(std:: type_info*)-1L)   // just a placeholder
#endif


// These will be helpful for detecting bugs, but AppleClang doesn't support them yet as of Xcode 16.
// <https://clang.llvm.org/docs/AttributeReference.html#id488>
#if __has_attribute(coro_return_type)
#   define coro_return_type_ [[clang::coro_return_type]]
#else
#   define coro_return_type_
#endif
#if __has_attribute(coro_wrapper)
#   define coro_wrapper_ [[clang::coro_wrapper]]
#else
#   define coro_wrapper_
#endif
#if __has_attribute(coro_lifetimebound)
#   define coro_lifetimebound_ [[clang::coro_lifetimebound]]
#else
#   define coro_lifetimebound_
#endif
#if __has_attribute(coro_disable_lifetimebound)
#   define coro_disable_lifetimebound_ [[clang::coro_disable_lifetimebound]]
#else
#   define coro_disable_lifetimebound_
#endif


// Synonyms for coroutine primitives. Optional, but they're more visible in the code.
#define AWAIT  co_await
#define YIELD  co_yield
#define RETURN co_return


namespace crouton {

    using std::string;
    using std::string_view;

    using coro_handle = CORO_NS::coroutine_handle<>;

    namespace mini {
        class ostream;
        class stringstream;
    }
    using ostream = crouton::mini::ostream;
    using stringstream = crouton::mini::stringstream;


    // `is_type_complete_v<T>` evaluates to true iff T is a complete (fully-defined) type.
    // By Raymond Chen: https://devblogs.microsoft.com/oldnewthing/20190710-00/?p=102678
    template<typename, typename = void>
    constexpr bool is_type_complete_v = false;
    template<typename T>
    constexpr bool is_type_complete_v<T, std::void_t<decltype(sizeof(T))>> = true;


    // `NonReference` is a concept that applies to any value that's not a reference.
    template <typename T>
    concept NonReference = !std::is_reference_v<T>;

}
