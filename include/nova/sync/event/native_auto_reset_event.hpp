// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <nova/sync/detail/compat.hpp>
#include <nova/sync/detail/native_handle_support.hpp>
#include <nova/sync/detail/timed_wait.hpp>

#if defined( __linux__ )
#    include <atomic>
#endif

namespace nova::sync {

/// @brief Auto-reset event backed by OS primitives (`eventfd`, `kqueue`, Win32 event).
///
/// Each `signal()` delivers exactly one token. If a thread is blocked in
/// `wait()`, it is woken and the token is consumed. Otherwise the token is
/// stored for the next `wait()` / `try_wait()` call.
class native_auto_reset_event
{
public:
#if defined( _WIN32 )
    using native_handle_type = void*;
    using duration_type      = std::chrono::milliseconds;
#else
    using duration_type      = std::chrono::nanoseconds;
    using native_handle_type = int;
#endif

#if defined( _WIN32 ) || defined( __APPLE__ )
    static constexpr bool is_async_capable = true;
#else
    static constexpr bool is_async_capable = false;
#endif

    /// @brief Constructs the event.
    /// @param initially_set When true the first wait() / try_wait() will succeed without blocking.
    explicit native_auto_reset_event( bool initially_set = false ) noexcept;

    ~native_auto_reset_event();

    native_auto_reset_event( const native_auto_reset_event& )            = delete;
    native_auto_reset_event& operator=( const native_auto_reset_event& ) = delete;

#if defined( _WIN32 ) || defined( __APPLE__ )
    /// @brief Returns the underlying OS native handle for event-loop integration.
    native_handle_type native_handle() const noexcept;
#endif

    /// @brief Delivers one token, waking exactly one waiter.
    void signal() noexcept;

    /// @brief Atomically consumes a token if one is pending.
    /// @return true if a token was available and consumed, false otherwise.
    [[nodiscard]] bool try_wait() noexcept;

    /// @brief Blocks until a token is available, then consumes it.
    void wait() noexcept;

    /// @brief Blocks until a token is available or the absolute deadline passes.
    /// @return true if a token was consumed, false if the deadline passed.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
#if defined( _WIN32 )
            return detail::wait_handle_until( native_handle(), abs_time );
#elif defined( __linux__ )
            if ( detail::ppoll_until( handle_.get(), abs_time ) )
                return try_wait();
            return false;
#elif defined( __APPLE__ )
            if ( detail::kevent_until( native_handle(), 1, abs_time ) )
                return try_wait();
            return false;
#endif
        }
        return try_wait_for( std::chrono::round< duration_type >( abs_time - Clock::now() ) );
    }

    /// @brief Blocks until a token is available or the timeout expires.
    /// @return true if a token was consumed, false if the timeout expired.
    [[nodiscard]] bool try_wait_for( duration_type ) noexcept;

private:
#if defined( _WIN32 )
    using scoped_handle = detail::scoped_handle;
    scoped_handle handle_;
#elif defined( __linux__ )
    using scoped_file_descriptor = detail::scoped_file_descriptor;
    scoped_file_descriptor  handle_;
    std::atomic< uint32_t > wait_count_ { 0 }; // Track if threads are waiting
#elif defined( __APPLE__ )
    using scoped_file_descriptor = detail::scoped_file_descriptor;
    scoped_file_descriptor handle_;
#else
    using scoped_file_descriptor = detail::scoped_file_descriptor;
    scoped_file_descriptor fds_[ 2 ];
#endif
};

} // namespace nova::sync
