// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Spinlock-based shared mutex.
class shared_spinlock_mutex
{
    // 32-bit State Bitmask Layout:
    // Bit 31: Exclusive Write Lock Active
    // Bit 30: Pending Write Request (Writer Fairness / Anti-Starvation)
    // Bits 0-29: Active Reader Count (Up to 1.07 billion concurrent readers)
    static constexpr uint32_t write_locked  = 1U << 31;
    static constexpr uint32_t write_pending = 1U << 30;
    static constexpr uint32_t readers_mask  = ~( write_locked | write_pending );

    std::atomic< uint32_t > state_ { 0 };

public:
    /// @brief Constructs an unlocked shared spinlock mutex.
    shared_spinlock_mutex()                                          = default;
    shared_spinlock_mutex( const shared_spinlock_mutex& )            = delete;
    shared_spinlock_mutex& operator=( const shared_spinlock_mutex& ) = delete;

    /// @brief Acquires the exclusive write lock.
    void lock() noexcept
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_strong(
                 expected, write_locked, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;

        lock_slow();
    }

    /// @brief Attempts to acquire the exclusive write lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked or readers present.
    [[nodiscard]] bool try_lock() noexcept
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );

        if ( ( expected & readers_mask ) == 0 && ( expected & write_locked ) == 0 ) {
            uint32_t desired = ( expected & ~write_pending ) | write_locked;
            return state_.compare_exchange_strong( expected,
                                                   desired,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed );
        }
        return false;
    }

    /// @brief Releases the exclusive write lock and wakes waiting readers.
    void unlock() noexcept
    {
        state_.fetch_and( ~write_locked, std::memory_order_release );
    }

    /// @brief Acquires a shared read lock (allows concurrent readers).
    void lock_shared() noexcept
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );

        if ( ( expected & ( write_locked | write_pending ) ) == 0 ) {
            if ( state_.compare_exchange_strong(
                     expected, expected + 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        }
        lock_shared_slow();
    }

    /// @brief Attempts to acquire a shared read lock without blocking.
    /// @return `true` if lock acquired, `false` if writer or pending writer present.
    bool try_lock_shared() noexcept
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        if ( ( expected & ( write_locked | write_pending ) ) == 0 ) {
            return state_.compare_exchange_strong( expected,
                                                   expected + 1,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed );
        }
        return false;
    }

    /// @brief Releases a shared read lock.
    void unlock_shared() noexcept
    {
        state_.fetch_sub( 1, std::memory_order_release );
    }

private:
    void lock_slow() noexcept;
    void lock_shared_slow() noexcept;
};

} // namespace nova::sync
