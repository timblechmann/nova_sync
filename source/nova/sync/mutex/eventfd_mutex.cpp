// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/eventfd_mutex.hpp>

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX

#    include <nova/sync/detail/backoff.hpp>

#    include <poll.h>
#    include <sys/eventfd.h>
#    include <unistd.h>

#    include <cassert>
#    include <nova/sync/detail/syscall.hpp>

namespace nova::sync {

eventfd_mutex::eventfd_mutex()
{
    evfd_ = ::eventfd( 0, EFD_NONBLOCK | EFD_SEMAPHORE );
    assert( evfd_ >= 0 && "eventfd() failed" );
}

eventfd_mutex::~eventfd_mutex()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void eventfd_mutex::unlock() noexcept
{
    uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );

    if ( prev > 1 ) {
        const uint64_t one = 1;
        detail::write_intr( evfd_, &one, sizeof( one ) );
    }
}

void eventfd_mutex::consume_lock() const noexcept
{
    uint64_t val = 0;
    detail::read_intr( evfd_, &val, sizeof( val ) );
}

void eventfd_mutex::lock_slow() noexcept
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

    s = state_.fetch_add( 2u, std::memory_order_relaxed ) + 2u;
    while ( true ) {
        if ( ( s & 1u ) == 0 ) {
            uint32_t desired = ( s - 2u ) | 1u;
            if ( state_.compare_exchange_weak( s, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
            continue;
        }

        // Block until the fd is readable, retrying on EINTR
        struct pollfd pfd {
            evfd_,
            POLLIN,
            0,
        };
        detail::poll_intr( &pfd, 1 );

        consume_lock();
        s = state_.load( std::memory_order_acquire );
    }
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
