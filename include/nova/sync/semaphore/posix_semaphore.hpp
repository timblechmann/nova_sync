// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

// POSIX named/unnamed semaphores are available on Linux and some BSDs.
// macOS has deprecated sem_init/sem_destroy and they do not work.
#if defined( __linux__ )
#  define NOVA_SYNC_HAS_POSIX_SEMAPHORE 1
#endif

#ifdef NOVA_SYNC_HAS_POSIX_SEMAPHORE

#  include <chrono>
#  include <cstddef>

#  include <semaphore.h>

namespace nova::sync {

/// Counting semaphore wrapping POSIX `sem_t`. Linux only (macOS has deprecated sem_init/sem_destroy).
/// Supports timed waits via `sem_timedwait`.
class posix_semaphore
{
public:
    explicit posix_semaphore( std::ptrdiff_t initial = 0 );
    ~posix_semaphore();
    posix_semaphore( const posix_semaphore& )            = delete;
    posix_semaphore& operator=( const posix_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept;

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept;

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept;

    template < class Clock, class Duration >
    [[nodiscard]] bool try_acquire_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock > ) {
            return try_acquire_until_system( abs_time );
        } else {
            auto remaining = abs_time - Clock::now();
            if ( remaining <= Clock::duration::zero() )
                return try_acquire();
            return try_acquire_for( std::chrono::duration_cast< std::chrono::nanoseconds >( remaining ) );
        }
    }

    template < class Rep, class Period >
    [[nodiscard]] bool try_acquire_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        auto abs = std::chrono::system_clock::now() + rel_time;
        return try_acquire_until_system( abs );
    }

private:
    bool try_acquire_until_system(
        std::chrono::time_point< std::chrono::system_clock, std::chrono::system_clock::duration > abs_time ) noexcept;

    sem_t sem_;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_POSIX_SEMAPHORE
