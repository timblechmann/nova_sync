// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <concepts>
#include <mutex>
#include <type_traits>
#include <utility>

namespace nova::sync::concepts {

// basic_lockable: requires lock() and unlock()
template < typename M >
concept basic_lockable = requires( M& m ) {
    { m.lock() };
    { m.unlock() };
};

// lockable: basic_lockable + try_lock() -> bool
template < typename M >
concept lockable = basic_lockable< M > && requires( M& m ) {
    { m.try_lock() } -> std::convertible_to< bool >;
};

// timed_lockable: lockable + try_lock_for and try_lock_until overloads
// We check for overloads accepting std::chrono types so both std::timed_mutex
// and custom timed mutexes are handled.
template < typename M >
concept timed_lockable = lockable< M > && requires( M& m ) {
    { m.try_lock_for( std::chrono::milliseconds( 0 ) ) } -> std::convertible_to< bool >;
    { m.try_lock_until( std::chrono::steady_clock::time_point {} ) } -> std::convertible_to< bool >;
};

// shared_lockable: shared ownership semantics
template < typename M >
concept shared_lockable = requires( M& m ) {
    { m.lock_shared() };
    { m.unlock_shared() };
    { m.try_lock_shared() } -> std::convertible_to< bool >;
};

// shared_timed_lockable: shared_lockable + timed shared try-locks
template < typename M >
concept shared_timed_lockable = shared_lockable< M > && requires( M& m ) {
    { m.try_lock_shared_for( std::chrono::milliseconds( 0 ) ) } -> std::convertible_to< bool >;
    { m.try_lock_shared_until( std::chrono::steady_clock::time_point {} ) } -> std::convertible_to< bool >;
};

// Convenience aliases for the concrete mutex categories
template < typename M >
concept mutex = lockable< M >;

template < typename M >
concept timed_mutex = timed_lockable< M >;

template < typename M >
concept shared_mutex = mutex< M > && shared_lockable< M >;

template < typename M >
concept shared_timed_mutex = timed_mutex< M > && shared_timed_lockable< M >;

// Trait used to mark recursive mutex types. Default is false.
template < typename T >
struct concepts_is_recursive : std::false_type
{};

template < typename T >
inline constexpr bool concepts_is_recursive_v = concepts_is_recursive< T >::value;

// recursive_mutex concept evaluates the trait
template < typename M >
concept recursive_mutex = mutex< M > && concepts_is_recursive_v< M >;

template <>
struct concepts_is_recursive< std::recursive_mutex > : std::true_type
{};

template <>
struct concepts_is_recursive< std::recursive_timed_mutex > : std::true_type
{};

template < typename M >
concept native_async_mutex = mutex< M > && requires( M& m ) {
    // The OS primitive (fd, HANDLE, …) used to signal waiters.
    typename M::native_handle_type;
    { std::as_const( m ).native_handle() } -> std::same_as< typename M::native_handle_type >;
};

/// @brief Refines `native_async_mutex` with async waiter registration.
///
/// Types satisfying this concept expose `add_async_waiter()`,
/// `remove_async_waiter()`, and `consume_lock()` for event-loop integration.
template < typename M >
concept async_waiter_mutex = native_async_mutex< M > && requires( M& m ) {
    { m.add_async_waiter() };
    { m.remove_async_waiter() };
    { m.consume_lock() };
};

} // namespace nova::sync::concepts
