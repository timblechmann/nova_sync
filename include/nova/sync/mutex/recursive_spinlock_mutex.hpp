// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <thread>

#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/mutex/tsa_annotations.hpp>

namespace nova::sync {

/// @brief Recursive spinlock-based mutex.
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY recursive_spinlock_mutex
{
    std::atomic< std::thread::id > owner_ { std::thread::id {} };
    std::size_t                    recursion_count_ { 0 };

public:
    /// @brief Constructs an unowned recursive spinlock mutex.
    recursive_spinlock_mutex()                                             = default;
    recursive_spinlock_mutex( const recursive_spinlock_mutex& )            = delete;
    recursive_spinlock_mutex& operator=( const recursive_spinlock_mutex& ) = delete;

    /// @brief Acquires the lock, allowing recursion from the current owner.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        const std::thread::id tid = std::this_thread::get_id();
        std::thread::id       expected {};

        if ( owner_.compare_exchange_strong( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return;
        }

        if ( expected == tid ) {
            ++recursion_count_;
            return;
        }

        lock_slow( tid );
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired or already owned by current thread, `false` otherwise.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        const std::thread::id tid = std::this_thread::get_id();
        std::thread::id       expected {};

        if ( owner_.compare_exchange_strong( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return true;
        }

        if ( expected == tid ) {
            ++recursion_count_;
            return true;
        }

        return false;
    }

    /// @brief Releases the lock (decrements recursion depth).
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        if ( --recursion_count_ == 0 )
            owner_.store( std::thread::id {}, std::memory_order_release );
    }

private:
    void lock_slow( std::thread::id tid ) noexcept;
};

namespace concepts {

template <>
struct concepts_is_recursive< nova::sync::recursive_spinlock_mutex > : std::true_type
{};

} // namespace concepts

} // namespace nova::sync
