// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Spinlock-based mutex.
class alignas( detail::hardware_destructive_interference_size ) spinlock_mutex
{
    std::atomic< bool > locked_ { false };

public:
    /// @brief Constructs an unlocked spinlock mutex.
    spinlock_mutex() = default;

    // Non-copyable & non-movable
    spinlock_mutex( const spinlock_mutex& )            = delete;
    spinlock_mutex& operator=( const spinlock_mutex& ) = delete;

    /// @brief Acquires the lock, spinning if necessary.
    void lock() noexcept
    {
        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;

        lock_slow();
    }

    /// @brief Releases the lock.
    void unlock() noexcept
    {
        locked_.store( false, std::memory_order_release );
    }

private:
    void lock_slow() noexcept;
};

} // namespace nova::sync
