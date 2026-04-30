// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include "nova/sync/event/timed_manual_reset_event.hpp"

namespace nova::sync::impl {

bool timed_manual_reset_event::on_timed_wait_timeout() noexcept
{
    uint32_t s = state_.load( std::memory_order_relaxed );
    while ( true ) {
        if ( s & flag_bit ) {
            if ( state_.compare_exchange_weak( s, s - waiter_one, std::memory_order_relaxed, std::memory_order_relaxed ) ) {
                sem_.acquire();
                std::atomic_thread_fence( std::memory_order_acquire );
                return true;
            }
        } else {
            if ( state_.compare_exchange_weak( s, s - waiter_one, std::memory_order_relaxed, std::memory_order_relaxed ) )
                return false;
        }
    }
}

} // namespace nova::sync::impl
