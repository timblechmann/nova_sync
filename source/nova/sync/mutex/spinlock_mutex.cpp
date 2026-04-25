// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/spinlock_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

NOVA_SYNC_NOINLINE
void spinlock_mutex::lock_slow() noexcept
{
    detail::exponential_backoff backoff;

    while ( true ) {
        while ( locked_.load( std::memory_order_relaxed ) )
            backoff.run();

        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;
    }
}

} // namespace nova::sync
