// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __linux__ )
#  define NOVA_SYNC_HAS_EVENTFD_SEMAPHORE 1
#endif

#ifdef NOVA_SYNC_HAS_EVENTFD_SEMAPHORE

#  include <chrono>
#  include <cstddef>
#  include <nova/sync/detail/compat.hpp>
#  include <nova/sync/detail/timed_wait.hpp>

namespace nova::sync {

/// Counting semaphore backed by Linux `eventfd(EFD_SEMAPHORE)`. Linux only.
/// Exposes a pollable file descriptor via `native_handle()` for async integration.
class eventfd_semaphore
{
public:
    /// File descriptor (POSIX).
    using native_handle_type = int;
    using duration_type      = std::chrono::nanoseconds;

    explicit eventfd_semaphore( std::ptrdiff_t initial = 0 );
    ~eventfd_semaphore();
    eventfd_semaphore( const eventfd_semaphore& )            = delete;
    eventfd_semaphore& operator=( const eventfd_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept;

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept;

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept;

    /// Blocks for up to @p rel_ns, attempting to acquire a token. Returns `true` if acquired, `false` if timed out.
    bool try_acquire_for( duration_type rel_ns ) noexcept
    {
        if ( rel_ns <= std::chrono::nanoseconds::zero() )
            return try_acquire();
        while ( !try_acquire() ) {
            if ( !detail::ppoll_for( evfd_, rel_ns ) )
                return try_acquire();
        }
        return true;
    }

    template < class Rep, class Period >
    bool try_acquire_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        return try_acquire_for( std::chrono::duration_cast< duration_type >( rel_time ) );
    }

    template < class Clock, class Duration >
    bool try_acquire_until( const std::chrono::time_point< Clock, Duration >& abs_time )
    {
        if ( try_acquire() )
            return true;

        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            while ( true ) {
                if ( !detail::ppoll_until( evfd_, abs_time ) )
                    return try_acquire();
                if ( try_acquire() )
                    return true;
            }
        } else {
            return try_acquire_for( std::chrono::duration_cast< duration_type >( abs_time - Clock::now() ) );
        }
    }

    /// Returns the underlying file descriptor (for async integration).
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return evfd_;
    }

private:
    const int evfd_ { -1 };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_SEMAPHORE
