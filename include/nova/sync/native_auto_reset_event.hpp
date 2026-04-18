// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Auto-reset event based on OS primitives.
///
/// Maps directly to OS-specific synchronization primitives (e.g. eventfd on Linux, kqueue on macOS, SetEvent on Windows).
/// The event starts in the "not set" state. signal() delivers exactly one
/// token: if a thread is already blocked in wait(), it is woken and the
/// token is consumed atomically. If no thread is waiting the token is
/// stored and consumed by the next call to wait() or try_wait().
class alignas( detail::hardware_destructive_interference_size ) native_auto_reset_event
{
public:
#if defined( _WIN32 )
    using native_handle_type = void*;
#else
    using native_handle_type = int;
#endif

    /// @brief Constructs the event.
    /// @param initially_set When true the first wait() / try_wait() will succeed without blocking.
    explicit native_auto_reset_event( bool initially_set = false ) noexcept;

    ~native_auto_reset_event();

    native_auto_reset_event( const native_auto_reset_event& )            = delete;
    native_auto_reset_event& operator=( const native_auto_reset_event& ) = delete;

    /// @brief Returns the underlying OS native handle.
    ///
    /// This handle can be used to wait for the event via an event loop,
    /// enabling integration with C++20 coroutines or C++26 executors.
    /// @return The native handle (file descriptor or HANDLE).
    native_handle_type native_handle() const noexcept;

    /// @brief Delivers one token, waking exactly one waiter.
    ///
    /// Idempotent when already set: a second signal() without an intervening
    /// wait() is discarded.
    void signal() noexcept;

    /// @brief Atomically consumes a token if one is pending.
    /// @return true if a token was available and consumed, false otherwise.
    bool try_wait() noexcept;

    /// @brief Blocks until a token is available, then consumes it.
    void wait() noexcept;

    /// @brief Blocks until a token is available or the absolute deadline passes.
    /// @return true if a token was consumed, false if the deadline passed.
    template < class Clock, class Duration >
    bool wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        return wait_for( abs_time - Clock::now() );
    }

    /// @brief Blocks until a token is available or the timeout expires.
    /// @return true if a token was consumed, false if the timeout expired.
    template < class Rep, class Period >
    bool wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return wait_for_impl( std::chrono::duration_cast< std::chrono::milliseconds >( rel_time ).count() );
    }

private:
    bool wait_for_impl( long long timeout_ms ) noexcept;

#if defined( _WIN32 )
    native_handle_type handle_;
#elif defined( __linux__ ) || defined( __APPLE__ )
    int handle_;
#else
    int fds_[ 2 ];
#endif
};

} // namespace nova::sync
