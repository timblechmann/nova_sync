// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/mutex/support/async_waiter_guard.hpp>

#if __cplusplus >= 202302L && __has_include( <expected> )
#    define NOVA_SYNC_HAS_EXPECTED
#    define NOVA_SYNC_HAS_STD_EXPECTED
#    include <expected>
#endif

#if __has_include( <tl/expected.hpp> )
#    ifndef NOVA_SYNC_HAS_EXPECTED
#        define NOVA_SYNC_HAS_EXPECTED
#    endif
#    define NOVA_SYNC_HAS_TL_EXPECTED
#    include <tl/expected.hpp>
#endif

#if defined( NOVA_SYNC_HAS_EXPECTED )

#    include <cassert>
#    include <functional>
#    include <mutex>
#    include <system_error>

namespace nova::sync::detail {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Invoke a handler with a successful expected<unique_lock, error_code> result.
///
/// Dispatches to the appropriate expected implementation (std::expected or
/// tl::expected) based on compile-time availability. Asserts if the handler
/// cannot be invoked with the expected type.
template < typename Mutex, typename Handler >
void invoke_with_lock( Handler&& handler, std::unique_lock< Mutex > lock )
{
#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type { std::move( lock ) } );
#    endif
#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type { std::move( lock ) } );
#    endif

    assert( false && "Handler must be invocable with expected<unique_lock, error_code>" );
#    ifdef __cpp_lib_unreachable
    std::unreachable();
#    endif
}

/// @brief Invoke a handler with an error expected<unique_lock, error_code> result.
///
/// Dispatches to the appropriate expected implementation (std::expected or
/// tl::expected) based on compile-time availability. Asserts if the handler
/// cannot be invoked with the expected type.
template < typename Mutex, typename Handler >
void invoke_with_error( Handler&& handler, std::error_code ec )
{
#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type { std::unexpect, ec } );
#    endif
#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type { tl::unexpect, ec } );
#    endif

    assert( false && "Handler must be invocable with expected<unique_lock, error_code>" );
#    ifdef __cpp_lib_unreachable
    std::unreachable();
#    endif
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
#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
      || std::invocable< Handler, std::expected< std::unique_lock< Mutex >, std::error_code > >
#    endif
#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
      || std::invocable< Handler, tl::expected< std::unique_lock< Mutex >, std::error_code > >
#    endif
    ;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


} // namespace nova::sync::detail

#endif // defined( NOVA_SYNC_HAS_EXPECTED )
