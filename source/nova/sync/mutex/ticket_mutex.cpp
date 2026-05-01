// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/ticket_mutex.hpp>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/pause.hpp>
#include <nova/sync/futex/atomic_wait.hpp>

namespace nova::sync {

template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
void ticket_mutex< Policies... >::lock_slow_plain( uint32_t my_ticket ) noexcept
{
    while ( true ) {
        uint32_t current_serving = serving_ticket_.load( std::memory_order_acquire );
        if ( current_serving == my_ticket )
            return;
        atomic_wait_for( serving_ticket_, current_serving, std::chrono::hours( 24 ) );
    }
}

template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
void ticket_mutex< Policies... >::lock_slow_backoff( uint32_t my_ticket ) noexcept
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

// Explicit instantiations
template class ticket_mutex<>;
template class ticket_mutex< with_backoff >;

} // namespace nova::sync
