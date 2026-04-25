// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/mutex/annotations.hpp>

namespace nova::sync {

/// @brief Fair mutex with FIFO lock acquisition order (ticket lock).
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
        uint32_t my_ticket = next_ticket_.fetch_add( 1, std::memory_order_seq_cst );

        if ( serving_ticket_.load( std::memory_order_seq_cst ) == my_ticket )
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

        serving_ticket_.store( next_serving, std::memory_order_seq_cst );

        if ( next_ticket_.load( std::memory_order_seq_cst ) != next_serving )
            serving_ticket_.notify_all();
    }

private:
    alignas( detail::hardware_destructive_interference_size ) std::atomic< uint32_t > serving_ticket_ { 0 };
    alignas( detail::hardware_destructive_interference_size ) std::atomic< uint32_t > next_ticket_ { 0 };

    void lock_slow( uint32_t my_ticket ) noexcept;
};

} // namespace nova::sync
