// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

/// @brief Fair mutex with FIFO lock acquisition order (ticket lock).
///
/// @note `try_lock_for` / `try_lock_until` do **not** acquire a ticket and
///       therefore do not participate in the FIFO order. They retry
///       `try_lock()` (opportunistically grabbing the next ticket only when
///       the lock is free) until the deadline, sleeping between attempts via
///       the futex-based `atomic_wait_until`. This avoids ticket starvation
///       but means timed waiters are not strictly fair against blocked
///       `lock()` callers.
class NOVA_SYNC_CAPABILITY( "mutex" ) fair_mutex
{
public:
    /// @brief Constructs an unlocked fair mutex.
    fair_mutex()                               = default;
    ~fair_mutex()                              = default;
    fair_mutex( const fair_mutex& )            = delete;
    fair_mutex& operator=( const fair_mutex& ) = delete;

    /// @brief Acquires the lock in FIFO order.
    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t my_ticket = next_ticket_.fetch_add( 1, std::memory_order_relaxed );

        if ( serving_ticket_.load( std::memory_order_acquire ) == my_ticket )
            return;

        lock_slow( my_ticket );
    }

    /// @brief Attempts to acquire the lock without waiting.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t current_serving = serving_ticket_.load( std::memory_order_acquire );
        uint32_t expected        = current_serving;

        return next_ticket_.compare_exchange_strong( expected,
                                                     current_serving + 1,
                                                     std::memory_order_acquire,
                                                     std::memory_order_relaxed );
    }

    /// @brief Releases the lock and serves the next waiting thread.
    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        uint32_t next_serving = serving_ticket_.load( std::memory_order_relaxed ) + 1;

        serving_ticket_.store( next_serving, std::memory_order_release );

        // Always notify: both ticket-queue waiters (lock_slow) and timed
        // waiters (try_lock_until) watch serving_ticket_.
        atomic_notify_all( serving_ticket_ );
    }

    /// @brief Tries to acquire the lock within a relative timeout.
    ///
    /// Does not take a ticket; see class documentation for fairness notes.
    /// @return `true` if lock acquired, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_lock_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    /// @brief Tries to acquire the lock until an absolute deadline.
    ///
    /// Does not take a ticket; see class documentation for fairness notes.
    /// @return `true` if lock acquired, `false` if timed out.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_lock_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        while ( true ) {
            if ( try_lock() )
                return true;

            uint32_t serving = serving_ticket_.load( std::memory_order_relaxed );

            // Return false if already past deadline
            if ( Clock::now() >= abs_time )
                return false;

            // Sleep until serving_ticket_ changes (= someone unlocked) or deadline
            atomic_wait_until( serving_ticket_, serving, abs_time );
        }
    }

private:
    alignas( detail::hardware_destructive_interference_size ) std::atomic< uint32_t > serving_ticket_ { 0 };
    alignas( detail::hardware_destructive_interference_size ) std::atomic< uint32_t > next_ticket_ { 0 };

    void lock_slow( uint32_t my_ticket ) noexcept;
};

} // namespace nova::sync
