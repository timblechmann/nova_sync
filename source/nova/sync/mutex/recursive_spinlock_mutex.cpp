// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/recursive_spinlock_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

NOVA_SYNC_NOINLINE
void nova::sync::recursive_spinlock_mutex::lock_slow( const std::thread::id tid ) noexcept
{
    detail::exponential_backoff backoff;
    const std::thread::id       empty_id {};

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


} // namespace nova::sync
