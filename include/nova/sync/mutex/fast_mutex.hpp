// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/mutex/annotations.hpp>

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
        if ( state_.compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) )
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
        uint32_t prev = state_.fetch_and( ~1, std::memory_order_release );

        if ( prev > 1 )
            state_.notify_one();
    }

private:
    // State layout:
    // Bit 0     : Lock status (0 = Free, 1 = Locked)
    // Bits 1-31 : Number of sleeping threads (Waiter count)
    std::atomic< uint32_t > state_ { 0 };

    void lock_slow( uint32_t expected ) noexcept;
};

} // namespace nova::sync
