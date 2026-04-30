// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include "nova/sync/event/timed_auto_reset_event.hpp"

namespace nova::sync::impl {

void timed_auto_reset_event::wait() noexcept
{
    if ( try_wait() )
        return;

    state_.fetch_add( 1u, std::memory_order_relaxed );

    uint32_t s = state_.load( std::memory_order_acquire );
    while ( s >= post_one ) {
        if ( state_.compare_exchange_weak( s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            std::ignore = sem_.try_acquire();
            return;
        }
    }

    while ( true ) {
        sem_.acquire();
        s = state_.load( std::memory_order_relaxed );
        while ( true ) {
            if ( s >= post_one ) {
                if ( state_.compare_exchange_weak(
                         s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) )
                    return;
            } else {
                break;
            }
        }
    }
}

bool timed_auto_reset_event::on_timed_wait_timeout() noexcept
{
    uint32_t s = state_.load( std::memory_order_relaxed );
    while ( true ) {
        if ( s >= post_one ) {
            if ( state_.compare_exchange_weak(
                     s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                sem_.acquire();
                return true;
            }
        } else {
            if ( state_.compare_exchange_weak( s, s - 1u, std::memory_order_relaxed, std::memory_order_relaxed ) )
                return false;
        }
    }
}

} // namespace nova::sync::impl
