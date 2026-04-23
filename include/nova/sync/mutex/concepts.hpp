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

// timed_lockable: lockable + try_lock_for/duration and try_lock_until/time_point
// Checks two distinct duration and time_point specialisations to enforce that
// the methods are proper templates (not overloads for a single concrete type).
// Also requires duration_type, a typedef exposing the effective timeout
// resolution of the implementation (e.g. nanoseconds for ppoll/kevent,
// milliseconds for WaitForSingleObject).
template < typename M >
concept timed_lockable = lockable< M > && requires( M& m ) {
    // duration_type: effective timeout granularity of the implementation
    typename M::duration_type;
    // try_lock_for must be a template accepting any duration specialisation
    { m.try_lock_for( typename M::duration_type {} ) } -> std::convertible_to< bool >;
    // try_lock_until must be a template accepting any time_point specialisation
    { m.try_lock_until( std::chrono::steady_clock::time_point {} ) } -> std::convertible_to< bool >;
    { m.try_lock_until( std::chrono::system_clock::time_point {} ) } -> std::convertible_to< bool >;
};

// shared_lockable: shared ownership semantics
template < typename M >
concept shared_lockable = requires( M& m ) {
    { m.lock_shared() };
    { m.unlock_shared() };
    { m.try_lock_shared() } -> std::convertible_to< bool >;
};

// shared_timed_lockable: shared_lockable + timed shared try-locks
// Checks two distinct specialisations to enforce genericity.
// Also requires duration_type consistent with timed_lockable.
template < typename M >
concept shared_timed_lockable = shared_lockable< M > && requires( M& m ) {
    typename M::duration_type;
    { m.try_lock_shared_for( typename M::duration_type {} ) } -> std::convertible_to< bool >;
    { m.try_lock_shared_until( std::chrono::steady_clock::time_point {} ) } -> std::convertible_to< bool >;
    { m.try_lock_shared_until( std::chrono::system_clock::time_point {} ) } -> std::convertible_to< bool >;
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

/// @brief Refines `native_async_mutex` with a user-space waiter count.
///
/// Types satisfying this concept expose `add_async_waiter()` and
/// `remove_async_waiter()` so that async integration layers can register
/// themselves in the waiter count.  This allows `unlock()` to skip the
/// kernel signal on the uncontended fast path while still notifying the
/// OS primitive when at least one async waiter is registered.
///
/// They also expose `consume_lock()` which drains a pending kernel
/// notification without blocking.  This must be called after acquiring
/// the lock via user-space CAS while registered as a waiter, because
/// `unlock()` may have already posted the notification (it saw waiters > 0)
/// before the CAS grabbed ownership.  Without draining, the stale
/// notification would cause spurious wakeups (or busy-loops in event-loop
/// integrations) for subsequent waiters.
///
/// Modelled by: `fast_eventfd_mutex` (Linux), `fast_kqueue_mutex` (Apple),
///              `win32_event_mutex` (Windows).
template < typename M >
concept async_waiter_mutex = native_async_mutex< M > && requires( M& m ) {
    { m.add_async_waiter() };
    { m.remove_async_waiter() };
    { m.consume_lock() };
};

} // namespace nova::sync::concepts
