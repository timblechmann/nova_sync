// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <nova/sync/detail/compat.hpp>
#include <nova/sync/detail/native_handle_support.hpp>
#include <nova/sync/detail/timed_wait.hpp>

namespace nova::sync {

/// @brief Manual-reset event backed by OS primitives (`eventfd`, `kqueue`, Win32 event).
///
/// Once `signal()` is called, all waiters are woken and subsequent `wait()` /
/// `try_wait()` calls return immediately until `reset()` is called.
class native_manual_reset_event
{
public:
#if defined( _WIN32 )
    using native_handle_type = void*;
    using duration_type      = std::chrono::milliseconds;
#else
    using duration_type      = std::chrono::nanoseconds;
    using native_handle_type = int;
#endif

    /// @brief Constructs the event in the "not set" state.
    /// @param initially_set If true, the event is initially set.
    explicit native_manual_reset_event( bool initially_set = false ) noexcept;

    ~native_manual_reset_event();

    native_manual_reset_event( const native_manual_reset_event& )            = delete;
    native_manual_reset_event& operator=( const native_manual_reset_event& ) = delete;

    /// @brief Returns the underlying OS native handle for event-loop integration.
    /// @return The native handle (file descriptor or HANDLE).
    native_handle_type native_handle() const noexcept;

    /// @brief Transitions the event to "set", waking all waiters.
    void signal() noexcept;

    /// @brief Transitions the event back to "not set".
    void reset() noexcept;

    /// @brief Returns true if the event is currently set, without blocking.
    [[nodiscard]] bool try_wait() const noexcept;

    /// @brief Blocks until the event is set.
    void wait() const noexcept;

    /// @brief Blocks until the event is set or the absolute deadline passes.
    /// @return true if the event was set, false if the deadline passed.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) const noexcept
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
#if defined( _WIN32 )
            return detail::wait_handle_until( native_handle(), abs_time );
#elif defined( __linux__ )
            return detail::ppoll_until( native_handle(), abs_time );
#elif defined( __APPLE__ )
            return detail::kevent_until( native_handle(), 1, abs_time );
#endif
        }
        return try_wait_for( std::chrono::round< duration_type >( abs_time - Clock::now() ) );
    }

    /// @brief Blocks until the event is set or the timeout expires.
    /// @return true if the event was set, false if the timeout expired.
    [[nodiscard]] bool try_wait_for( duration_type ) const noexcept;

private:
#if defined( _WIN32 )
    using scoped_handle = detail::scoped_handle;
    scoped_handle handle_;
#elif defined( __linux__ ) || defined( __APPLE__ )
    // Use scoped_file_descriptor to manage the lifetime of the native fd
    using scoped_file_descriptor = detail::scoped_file_descriptor;
    scoped_file_descriptor handle_;
#else
    using scoped_file_descriptor = detail::scoped_file_descriptor;
    scoped_file_descriptor fds_[ 2 ];
#endif
};

} // namespace nova::sync
