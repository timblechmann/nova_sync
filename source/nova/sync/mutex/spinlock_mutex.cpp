// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/spinlock_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/pause.hpp>

namespace nova::sync::impl {

//----------------------------------------------------------------------------------------------------------------------
// spinlock_plain

void spinlock_plain::lock_slow() noexcept
{
    while ( true ) {
        while ( locked_.load( std::memory_order_relaxed ) )
            detail::pause();
        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// spinlock_backoff

void spinlock_backoff::lock_slow() noexcept
{
    detail::exponential_backoff backoff;
    while ( true ) {
        while ( locked_.load( std::memory_order_relaxed ) )
            backoff.run();
        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// recursive_spinlock_plain

void recursive_spinlock_plain::lock_slow( std::thread::id tid ) noexcept
{
    const std::thread::id empty_id {};
    while ( true ) {
        while ( owner_.load( std::memory_order_relaxed ) != empty_id )
            detail::pause();

        std::thread::id expected = empty_id;
        if ( owner_.compare_exchange_weak( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// recursive_spinlock_backoff

void recursive_spinlock_backoff::lock_slow( std::thread::id tid ) noexcept
{
    const std::thread::id       empty_id {};
    detail::exponential_backoff backoff;
    while ( true ) {
        while ( owner_.load( std::memory_order_relaxed ) != empty_id )
            backoff.run();

        std::thread::id expected = empty_id;
        if ( owner_.compare_exchange_weak( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------
// shared_spinlock_plain

void shared_spinlock_plain::lock_slow() noexcept
{
    while ( true ) {
        uint32_t expected = state_.load( std::memory_order_relaxed );

        while ( expected & write_locked ) {
            detail::pause();
            expected = state_.load( std::memory_order_relaxed );
        }

        if ( ( expected & readers_mask ) > 0 ) {
            if ( ( expected & write_pending ) == 0 ) {
                if ( !state_.compare_exchange_weak( expected, expected | write_pending, std::memory_order_relaxed ) )
                    continue;
                expected |= write_pending;
            }

            while ( ( ( expected & readers_mask ) > 0 ) || ( expected & write_locked ) ) {
                detail::pause();
                expected = state_.load( std::memory_order_relaxed );
            }
        }

        uint32_t desired = ( expected & ~write_pending ) | write_locked;
        if ( state_.compare_exchange_weak( expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
    }
}

void shared_spinlock_plain::lock_shared_slow() noexcept
{
    while ( true ) {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        while ( expected & ( write_locked | write_pending ) ) {
            detail::pause();
            expected = state_.load( std::memory_order_relaxed );
        }
        if ( state_.compare_exchange_weak( expected, expected + 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
    }
}

//----------------------------------------------------------------------------------------------------------------------
// shared_spinlock_backoff

void shared_spinlock_backoff::lock_slow() noexcept
{
    detail::exponential_backoff backoff;
    while ( true ) {
        uint32_t expected = state_.load( std::memory_order_relaxed );

        while ( expected & write_locked ) {
            backoff.run();
            expected = state_.load( std::memory_order_relaxed );
        }

        if ( ( expected & readers_mask ) > 0 ) {
            if ( ( expected & write_pending ) == 0 ) {
                if ( !state_.compare_exchange_weak( expected, expected | write_pending, std::memory_order_relaxed ) )
                    continue;
                expected |= write_pending;
            }

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

void shared_spinlock_backoff::lock_shared_slow() noexcept
{
    detail::exponential_backoff backoff;
    while ( true ) {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        while ( expected & ( write_locked | write_pending ) ) {
            backoff.run();
            expected = state_.load( std::memory_order_relaxed );
        }
        if ( state_.compare_exchange_weak( expected, expected + 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
    }
}

} // namespace nova::sync::impl
