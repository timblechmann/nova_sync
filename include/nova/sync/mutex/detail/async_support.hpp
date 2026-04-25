// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/detail/async_support.hpp>
#include <nova/sync/mutex/annotations.hpp>
#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/mutex/support/async_waiter_guard.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED )

#  include <functional>
#  include <mutex>
#  include <system_error>

namespace nova::sync::detail {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template < typename Mutex, typename Handler >
void invoke_with_lock( Handler&& handler, std::unique_lock< Mutex > lock )
{
    invoke_handler_success< std::unique_lock< Mutex >, Handler >( std::forward< Handler >( handler ),
                                                                  std::move( lock ) );
}

template < typename Mutex, typename Handler >
void invoke_with_error( Handler&& handler, std::error_code ec )
{
    invoke_handler_error< std::unique_lock< Mutex >, Handler >( std::forward< Handler >( handler ), ec );
}

template < typename Mutex, typename Handler >
void invoke_with_error( Handler&& handler, std::errc ec )
{
    invoke_with_error< Mutex, Handler >( std::forward< Handler >( handler ), std::make_error_code( ec ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Try to acquire the lock after the kernel primitive signals readability.
///
/// Delegates to `async_waiter_guard::try_acquire()`.  Prefer using
/// `async_waiter_guard` directly; this free function exists for call-sites
/// that cannot easily carry a guard reference.
///
/// Attempts to acquire the lock and, on success, drains any pending kernel
/// notifications to prevent spurious wakeups for other waiters.
///
/// @return `true` if the lock was successfully acquired.
inline bool platform_try_acquire_after_wait( auto& mtx )
{
    if ( !mtx.try_lock() )
        return false; // spurious wakeup — retry

    // For async_waiter_mutex types, drain the kernel notification.
    if constexpr ( concepts::async_waiter_mutex< std::remove_reference_t< decltype( mtx ) > > )
        mtx.consume_lock();

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Register an async waiter with a mutex (if supported).
///
/// For mutexes that support the `async_waiter_mutex` concept, increments
/// the waiter count. For simple mutexes, this is a no-op.
/// @deprecated Prefer `async_waiter_guard` which manages the lifetime automatically.
template < typename Mutex >
inline void register_async_waiter( Mutex& mtx ) noexcept
{
    if constexpr ( concepts::async_waiter_mutex< Mutex > )
        mtx.add_async_waiter();
}

/// @brief Unregister an async waiter from a mutex (if supported).
///
/// For mutexes that support the `async_waiter_mutex` concept, decrements
/// the waiter count. For simple mutexes, this is a no-op.
/// @deprecated Prefer `async_waiter_guard` which manages the lifetime automatically.
template < typename Mutex >
inline void unregister_async_waiter( Mutex& mtx ) noexcept
{
    if constexpr ( concepts::async_waiter_mutex< Mutex > )
        mtx.remove_async_waiter();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Higher-order function: registers a waiter, calls @p fn, then
///        unregisters. Returns the result of @p fn.
///
/// Equivalent to:
/// @code
///   async_waiter_guard guard(mtx);
///   return std::invoke(fn);
/// @endcode
///
/// The guard is released after @p fn returns (or throws).
template < typename Mutex, typename Fn >
decltype( auto ) with_async_waiter( Mutex& mtx, Fn&& fn )
{
    async_waiter_guard< Mutex > guard( mtx );
    return std::invoke( std::forward< Fn >( fn ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template < typename Handler, typename Mutex >
concept invocable_with_expected
    = false // Base case so we can cleanly chain || with macros
#  ifdef NOVA_SYNC_HAS_STD_EXPECTED
      || std::invocable< Handler, std::expected< std::unique_lock< Mutex >, std::error_code > >
#  endif
#  ifdef NOVA_SYNC_HAS_TL_EXPECTED
      || std::invocable< Handler, tl::expected< std::unique_lock< Mutex >, std::error_code > >
#  endif
    ;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


} // namespace nova::sync::detail

#endif // defined( NOVA_SYNC_HAS_EXPECTED )
