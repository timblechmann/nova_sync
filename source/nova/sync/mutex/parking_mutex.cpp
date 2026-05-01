// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/parking_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>

namespace nova::sync::impl {

//----------------------------------------------------------------------------------------------------------------------
// parking_mutex_plain — no backoff

NOVA_SYNC_NOINLINE
void parking_mutex_plain::lock_slow( uint32_t expected ) noexcept
{
    // Spinning failed, now we need to sleep.
    // To register ourselves as a waiter, we add 2, which increments the waiter count in the upper 31 bits.
    state_.fetch_add( 2, std::memory_order_relaxed );
    expected = state_.load( std::memory_order_relaxed );

    while ( true ) {
        if ( ( expected & 1 ) == 0 ) {
            // Lock is free. Acquire the lock AND unregister ourselves as a waiter.
            uint32_t desired = ( expected - 2 ) | 1;
            if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        } else {
#ifdef __linux__
            // On Linux, use atomic_wait (futex-based) for better performance
            atomic_wait( state_, expected, std::memory_order_relaxed );
#else
            // On other platforms, use std::atomic::wait
            state_.wait( expected, std::memory_order_relaxed );
#endif
            expected = state_.load( std::memory_order_relaxed );
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// parking_mutex_backoff — exponential backoff

NOVA_SYNC_NOINLINE
void parking_mutex_backoff::lock_slow( uint32_t expected ) noexcept
{
    detail::exponential_backoff backoff;

    while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
        if ( ( expected & 1 ) == 0 ) {
            if ( state_.compare_exchange_weak(
                     expected, expected | 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
            // CAS failed — expected was updated; re-evaluate immediately without backing off
            continue;
        }

        backoff.run();
        expected = state_.load( std::memory_order_relaxed );
    }

    // Spinning exhausted — register as waiter and park
    state_.fetch_add( 2, std::memory_order_relaxed );
    expected = state_.load( std::memory_order_relaxed );

    while ( true ) {
        if ( ( expected & 1 ) == 0 ) {
            uint32_t desired = ( expected - 2 ) | 1;
            if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        } else {
#ifdef __linux__
            // On Linux, use atomic_wait (futex-based) for better performance
            atomic_wait( state_, expected, std::memory_order_relaxed );
#else
            // On other platforms, use std::atomic::wait
            state_.wait( expected, std::memory_order_relaxed );
#endif
            expected = state_.load( std::memory_order_relaxed );
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// parking_mutex_timed — no backoff, futex-based atomic_wait

NOVA_SYNC_NOINLINE
void parking_mutex_timed::lock_slow( uint32_t expected ) noexcept
{
    state_.fetch_add( 2, std::memory_order_relaxed );
    expected = state_.load( std::memory_order_relaxed );

    while ( true ) {
        if ( ( expected & 1 ) == 0 ) {
            uint32_t desired = ( expected - 2 ) | 1;
            if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        } else {
            atomic_wait( state_, expected, std::memory_order_relaxed );
            expected = state_.load( std::memory_order_relaxed );
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// parking_mutex_timed_backoff — exponential backoff, futex-based atomic_wait

NOVA_SYNC_NOINLINE
void parking_mutex_timed_backoff::lock_slow( uint32_t expected ) noexcept
{
    detail::exponential_backoff backoff;

    while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
        if ( ( expected & 1 ) == 0 ) {
            if ( state_.compare_exchange_weak(
                     expected, expected | 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
            // CAS failed — expected was updated; re-evaluate immediately without backing off
            continue;
        }

        backoff.run();
        expected = state_.load( std::memory_order_relaxed );
    }

    // Spinning exhausted — register as waiter and park
    state_.fetch_add( 2, std::memory_order_relaxed );
    expected = state_.load( std::memory_order_relaxed );

    while ( true ) {
        if ( ( expected & 1 ) == 0 ) {
            uint32_t desired = ( expected - 2 ) | 1;
            if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        } else {
            atomic_wait( state_, expected, std::memory_order_relaxed );
            expected = state_.load( std::memory_order_relaxed );
        }
    }
}

} // namespace nova::sync::impl
