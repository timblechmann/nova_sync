// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/fast_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

NOVA_SYNC_NOINLINE
void fast_mutex::lock_slow( uint32_t expected ) noexcept
{
    detail::exponential_backoff backoff;
    while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
        if ( ( expected & 1 ) == 0 ) {
            if ( state_.compare_exchange_weak(
                     expected, expected | 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;

            // CAS failed (c was updated). Re-evaluate immediately without backing off.
            continue;
        }

        backoff.run();

        expected = state_.load( std::memory_order_relaxed );
    }

    // Spinning failed, now we need to sleep.
    // To register ourselves as a waiter, we add 2, which increments the waiter count in the upper 31 bits.
    state_.fetch_add( 2, std::memory_order_relaxed );
    expected = state_.load( std::memory_order_relaxed );

    while ( true ) {
        if ( ( expected & 1 ) == 0 ) {
            // Lock is free. Acquire the lock AND unregister ourselves as a waiter
            uint32_t desired = ( expected - 2 ) | 1;
            if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        } else {
            atomic_wait_for( state_, expected, std::chrono::hours( 24 ) );
            expected = state_.load( std::memory_order_relaxed );
        }
    }
}

} // namespace nova::sync
