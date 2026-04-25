// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/shared_spinlock_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

NOVA_SYNC_NOINLINE
void shared_spinlock_mutex::lock_slow() noexcept
{
    detail::exponential_backoff backoff;

    while ( true ) {
        uint32_t expected = state_.load( std::memory_order_relaxed );

        // Spin while another writer holds the lock
        while ( expected & write_locked ) {
            backoff.run();
            expected = state_.load( std::memory_order_relaxed );
        }

        if ( ( expected & readers_mask ) > 0 ) {
            // Broadcast to new readers that a writer is waiting.
            if ( ( expected & write_pending ) == 0 ) {
                if ( !state_.compare_exchange_weak( expected, expected | write_pending, std::memory_order_relaxed ) )
                    continue;

                expected |= write_pending;
            }

            // Spin until readers drain to 0 AND no other writer sneaks in.
            while ( ( ( expected & readers_mask ) > 0 ) || ( expected & write_locked ) ) {
                backoff.run();
                expected = state_.load( std::memory_order_relaxed );
            }
        }

        uint32_t desired = ( expected & ~write_pending ) | write_locked;
        if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
    }
}

NOVA_SYNC_NOINLINE
void shared_spinlock_mutex::lock_shared_slow() noexcept
{
    detail::exponential_backoff backoff;

    while ( true ) {
        uint32_t expected = state_.load( std::memory_order_relaxed );

        // Wait while a writer is active OR waiting
        while ( expected & ( write_locked | write_pending ) ) {
            backoff.run();
            expected = state_.load( std::memory_order_relaxed );
        }

        // Try to increment the reader count
        if ( state_.compare_exchange_weak(
                 expected, expected + 1, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            return;
        }
    }
}


} // namespace nova::sync
