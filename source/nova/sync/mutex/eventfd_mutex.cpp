// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/eventfd_mutex.hpp>

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX

#  include <nova/sync/detail/backoff.hpp>
#  include <nova/sync/detail/syscall.hpp>
#  include <nova/sync/detail/timed_wait.hpp>
#  include <nova/sync/mutex/support/async_waiter_guard.hpp>

#  include <poll.h>
#  include <sys/eventfd.h>
#  include <unistd.h>

#  include <cassert>

namespace nova::sync::detail {

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// eventfd_mutex_impl — simple poll-based variant, no user-space waiter count
// ---------------------------------------------------------------------------

eventfd_mutex_impl::eventfd_mutex_impl() :
    evfd_ { ::eventfd( 1, EFD_NONBLOCK | EFD_SEMAPHORE ) }
{
    assert( evfd_ >= 0 && "eventfd() failed" );
}

eventfd_mutex_impl::~eventfd_mutex_impl()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void eventfd_mutex_impl::lock() noexcept
{
    while ( !try_lock() ) {
        struct pollfd pfd {
            evfd_,
            POLLIN,
            0,
        };
        detail::poll_intr( pfd );
    }
}

bool eventfd_mutex_impl::try_lock() noexcept
{
    std::array< uint64_t, 1 > val {};
    return detail::read_intr( evfd_, std::as_writable_bytes( std::span( val ) ) ) == ssize_t( sizeof( uint64_t ) );
}

void eventfd_mutex_impl::unlock() noexcept
{
    const std::array< uint64_t, 1 > one { 1 };
    detail::write_intr( evfd_, std::as_bytes( std::span( one ) ) );
}

// ---------------------------------------------------------------------------
// fast_eventfd_mutex_impl — fast-path variant with user-space waiter count
// ---------------------------------------------------------------------------

fast_eventfd_mutex_impl::fast_eventfd_mutex_impl()
{
    evfd_ = ::eventfd( 0, EFD_NONBLOCK | EFD_SEMAPHORE );
    assert( evfd_ >= 0 && "eventfd() failed" );
}

fast_eventfd_mutex_impl::~fast_eventfd_mutex_impl()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void fast_eventfd_mutex_impl::unlock() noexcept
{
    uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );

    if ( prev > 1 ) {
        const std::array< uint64_t, 1 > one { 1 };
        detail::write_intr( evfd_, std::as_bytes( std::span( one ) ) );
    }
}

void fast_eventfd_mutex_impl::consume_lock() const noexcept
{
    std::array< uint64_t, 1 > val {};
    detail::read_intr( evfd_, std::as_writable_bytes( std::span( val ) ) );
}

void fast_eventfd_mutex_impl::lock_slow() noexcept
{
    detail::exponential_backoff backoff;

    uint32_t s = state_.load( std::memory_order_relaxed );
    while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
        if ( ( s & 1u ) == 0 ) {
            if ( state_.compare_exchange_weak( s, s | 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
            continue;
        }

        backoff.run();
        s = state_.load( std::memory_order_relaxed );
    }

    s = add_async_waiter();
    detail::async_waiter_guard< fast_eventfd_mutex_impl > guard( *this, detail::adopt_async_waiter );

    while ( true ) {
        if ( ( s & 1u ) == 0 ) {
            uint32_t desired = ( s - 2u ) | 1u;
            if ( state_.compare_exchange_weak( s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                consume_lock();
                guard.dismiss();
                return;
            }
            continue;
        }

        struct pollfd pfd {
            evfd_,
            POLLIN,
            0,
        };
        detail::poll_intr( pfd );

        consume_lock();
        s = state_.load( std::memory_order_acquire );
    }
}

bool fast_eventfd_mutex_impl::try_lock_for_ns( duration_type rel ) noexcept
{
    if ( rel <= 0ns )
        return try_lock();

    auto                                                  s = add_async_waiter();
    detail::async_waiter_guard< fast_eventfd_mutex_impl > guard( *this, detail::adopt_async_waiter );

    while ( true ) {
        if ( ( s & 1u ) == 0 ) {
            uint32_t desired = ( s - 2u ) | 1u;
            if ( state_.compare_exchange_weak( s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                consume_lock();
                guard.dismiss();
                return true;
            }
            continue;
        }

        if ( !detail::ppoll_for( evfd_, rel ) ) {
            return false;
        }

        consume_lock();
        s = state_.load( std::memory_order_acquire );
    }
}

} // namespace nova::sync::detail

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
