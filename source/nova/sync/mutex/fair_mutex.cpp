// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/fair_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

NOVA_SYNC_NOINLINE
void fair_mutex::lock_slow( uint32_t my_ticket ) noexcept
{
    detail::exponential_backoff backoff;

    while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
        if ( serving_ticket_.load( std::memory_order_acquire ) == my_ticket )
            return;

        backoff.run();
    }

    while ( true ) {
        uint32_t current_serving = serving_ticket_.load( std::memory_order_acquire );

        if ( current_serving == my_ticket )
            return;

        atomic_wait_for( serving_ticket_, current_serving, std::chrono::hours( 24 ) );
    }
}

} // namespace nova::sync
