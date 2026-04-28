// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/mutex/tsa_annotations.hpp>

namespace nova::sync {

/// @brief Spinlock-based mutex.
class NOVA_SYNC_CAPABILITY( "mutex" ) spinlock_mutex
{
    std::atomic< bool > locked_ { false };

public:
    /// @brief Constructs an unlocked spinlock mutex.
    spinlock_mutex() = default;

    // Non-copyable & non-movable
    spinlock_mutex( const spinlock_mutex& )            = delete;
    spinlock_mutex& operator=( const spinlock_mutex& ) = delete;

    /// @brief Acquires the lock, spinning if necessary.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;

        lock_slow();
    }

    /// @brief Tries to acquire the lock without spinning.
    /// @return true if the lock was successfully acquired, false otherwise.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( locked_.load( std::memory_order_relaxed ) )
            return false;

        return !locked_.exchange( true, std::memory_order_acquire );
    }

    /// @brief Releases the lock.
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        locked_.store( false, std::memory_order_release );
    }

private:
    void lock_slow() noexcept;
};

} // namespace nova::sync
