// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __APPLE__ )
#  define NOVA_SYNC_HAS_MACH_SEMAPHORE 1
#endif

#ifdef NOVA_SYNC_HAS_MACH_SEMAPHORE

#  include <chrono>
#  include <cstddef>

#  include <mach/mach.h>
#  include <mach/semaphore.h>

namespace nova::sync {

/// Counting semaphore wrapping Mach `semaphore_create`. Apple only (macOS/iOS).
/// Supports timed waits via `semaphore_timedwait`.
class mach_semaphore
{
public:
    explicit mach_semaphore( std::ptrdiff_t initial = 0 );
    ~mach_semaphore();
    mach_semaphore( const mach_semaphore& )            = delete;
    mach_semaphore& operator=( const mach_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept;

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept;

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept;

    template < class Rep, class Period >
    [[nodiscard]] bool try_acquire_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        auto ns   = std::chrono::duration_cast< std::chrono::nanoseconds >( rel_time );
        auto secs = std::chrono::duration_cast< std::chrono::seconds >( ns );
        auto rem  = ns - secs;

        mach_timespec_t ts;
        ts.tv_sec  = unsigned( secs.count() );
        ts.tv_nsec = clock_res_t( rem.count() );
        return timed_wait( ts );
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
    bool timed_wait( const mach_timespec_t& wait_time ) noexcept;

    semaphore_t sem_;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_MACH_SEMAPHORE
