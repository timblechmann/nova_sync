// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Manual-reset event based on OS primitives.
///
/// Maps directly to OS-specific synchronization primitives (e.g. eventfd on Linux, kqueue on macOS, SetEvent on
/// Windows). The event starts in the "not set" state. Once signal() is called every thread currently blocked in wait()
/// is woken and every subsequent call to wait() / try_wait() returns immediately — until reset() is called.
class alignas( detail::hardware_destructive_interference_size ) native_manual_reset_event
{
public:
#if defined( _WIN32 )
    using native_handle_type = void*;
#else
    using native_handle_type = int;
#endif

    /// @brief Constructs the event in the "not set" state.
    /// @param initially_set If true, the event is initially set.
    explicit native_manual_reset_event( bool initially_set = false ) noexcept;

    ~native_manual_reset_event();

    native_manual_reset_event( const native_manual_reset_event& )            = delete;
    native_manual_reset_event& operator=( const native_manual_reset_event& ) = delete;

    /// @brief Returns the underlying OS native handle.
    ///
    /// This handle can be used to wait for the event via an event loop,
    /// enabling integration with C++20 coroutines or C++26 executors.
    /// @return The native handle (file descriptor or HANDLE).
    native_handle_type native_handle() const noexcept;

    /// @brief Transitions the event to "set", waking all waiters.
    ///
    /// Idempotent: calling signal() on an already-set event is a no-op.
    void signal() noexcept;

    /// @brief Transitions the event back to "not set".
    void reset() noexcept;

    /// @brief Returns true if the event is currently set, without blocking.
    bool try_wait() const noexcept;

    /// @brief Blocks until the event is set.
    void wait() const noexcept;

    /// @brief Blocks until the event is set or the absolute deadline passes.
    /// @return true if the event was set, false if the deadline passed.
    template < class Clock, class Duration >
    bool wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) const noexcept
    {
        return wait_for( abs_time - Clock::now() );
    }

    /// @brief Blocks until the event is set or the timeout expires.
    /// @return true if the event was set, false if the timeout expired.
    template < class Rep, class Period >
    bool wait_for( std::chrono::duration< Rep, Period > const& rel_time ) const noexcept
    {
        return wait_for_impl( std::chrono::duration_cast< std::chrono::milliseconds >( rel_time ).count() );
    }

private:
    bool wait_for_impl( long long timeout_ms ) const noexcept;

#if defined( _WIN32 )
    native_handle_type handle_;
#elif defined( __linux__ ) || defined( __APPLE__ )
    int handle_;
#else
    int fds_[ 2 ];
#endif
};

} // namespace nova::sync
