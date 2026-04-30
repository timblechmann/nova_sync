// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>

#include <nova/sync/futex/atomic_wait.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

/// @brief Fast mutex with spinning and park/unpark.
class NOVA_SYNC_CAPABILITY( "mutex" ) fast_mutex
{
public:
    /// @brief Constructs an unlocked fast mutex.
    fast_mutex()                               = default;
    ~fast_mutex()                              = default;
    fast_mutex( const fast_mutex& )            = delete;
    fast_mutex& operator=( const fast_mutex& ) = delete;

    /// @brief Acquires the lock, spinning and parking as necessary.
    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;

        lock_slow( expected );
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t expected = 0;
        return state_.compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed );
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );

        if ( prev > 1 )
            atomic_notify_one( state_ );
    }

    /// @brief Tries to acquire the lock within a relative timeout.
    /// @return `true` if lock acquired, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_lock_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    /// @brief Tries to acquire the lock until an absolute deadline.
    /// @return `true` if lock acquired, `false` if timed out.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_lock_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        // Fast path
        if ( try_lock() )
            return true;

        return lock_slow_until( abs_time );
    }

private:
    // State layout:
    // Bit 0     : Lock status (0 = Free, 1 = Locked)
    // Bits 1-31 : Number of sleeping threads (Waiter count)
    std::atomic< uint32_t > state_ { 0 };

    void lock_slow( uint32_t expected ) noexcept;

    template < class Clock, class Duration >
    bool lock_slow_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        // Register as waiter
        state_.fetch_add( 2, std::memory_order_relaxed );
        uint32_t expected = state_.load( std::memory_order_relaxed );

        while ( true ) {
            if ( ( expected & 1 ) == 0 ) {
                uint32_t desired = ( expected - 2 ) | 1;
                if ( state_.compare_exchange_weak(
                         expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                    return true;
                continue; // CAS failed, retry
            }

            if ( !atomic_wait_until( state_, expected, abs_time ) ) {
                // Timed out — undo waiter registration
                expected = state_.load( std::memory_order_relaxed );
                while ( true ) {
                    // Try to grab lock while unregistering
                    if ( ( expected & 1 ) == 0 ) {
                        uint32_t desired = ( expected - 2 ) | 1;
                        if ( state_.compare_exchange_weak(
                                 expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                            return true; // grabbed it at last moment
                        continue;
                    }
                    // Lock still held: just decrement waiter count
                    if ( state_.compare_exchange_weak(
                             expected, expected - 2, std::memory_order_relaxed, std::memory_order_relaxed ) )
                        return false;
                }
            }

            expected = state_.load( std::memory_order_relaxed );
        }
    }
};

} // namespace nova::sync
