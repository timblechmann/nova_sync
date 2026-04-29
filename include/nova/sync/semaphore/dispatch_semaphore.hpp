// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __APPLE__ )
#  define NOVA_SYNC_HAS_DISPATCH_SEMAPHORE 1
#endif

#ifdef NOVA_SYNC_HAS_DISPATCH_SEMAPHORE

#  include <chrono>
#  include <cstddef>
#  include <cstdint>

#  include <dispatch/dispatch.h>

namespace nova::sync {

/// Counting semaphore wrapping Apple libdispatch `dispatch_semaphore_t`. Apple only (macOS/iOS).
/// Supports timed waits via `dispatch_semaphore_wait` with timeout.
class dispatch_semaphore
{
public:
    explicit dispatch_semaphore( std::ptrdiff_t initial = 0 );
    ~dispatch_semaphore();
    dispatch_semaphore( const dispatch_semaphore& )            = delete;
    dispatch_semaphore& operator=( const dispatch_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept;

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept;

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept;

    template < class Rep, class Period >
    [[nodiscard]] bool try_acquire_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        auto ns = std::chrono::duration_cast< std::chrono::nanoseconds >( rel_time );
        if ( ns <= std::chrono::nanoseconds::zero() )
            return try_acquire();
        dispatch_time_t timeout = dispatch_time( DISPATCH_TIME_NOW, int64_t( ns.count() ) );
        return dispatch_semaphore_wait( sem_, timeout ) == 0;
    }

    template < class Clock, class Duration >
    [[nodiscard]] bool try_acquire_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        auto remaining = abs_time - Clock::now();
        if ( remaining <= Clock::duration::zero() )
            return try_acquire();
        return try_acquire_for( remaining );
    }

private:
    dispatch_semaphore_t sem_;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_DISPATCH_SEMAPHORE
