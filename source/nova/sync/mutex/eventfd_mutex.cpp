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

namespace nova::sync {

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// eventfd_mutex — simple poll-based variant, no user-space waiter count
// ---------------------------------------------------------------------------

eventfd_mutex::eventfd_mutex() :
    evfd_ { ::eventfd( 1, EFD_NONBLOCK | EFD_SEMAPHORE ) }
{
    assert( evfd_ >= 0 && "eventfd() failed" );
}

eventfd_mutex::~eventfd_mutex()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void eventfd_mutex::lock() noexcept
{
    while ( !try_lock() ) {
        struct pollfd pfd {
            evfd_,
            POLLIN,
            0,
        };
        detail::poll_intr( &pfd, 1 );
    }
}

bool eventfd_mutex::try_lock() noexcept
{
    uint64_t val = 0;
    return detail::read_intr( evfd_, &val, sizeof( val ) ) == sizeof( val );
}

void eventfd_mutex::unlock() noexcept
{
    const uint64_t one = 1;
    detail::write_intr( evfd_, &one, sizeof( one ) );
}

// ---------------------------------------------------------------------------
// fast_eventfd_mutex — fast-path variant with user-space waiter count
// ---------------------------------------------------------------------------

fast_eventfd_mutex::fast_eventfd_mutex()
{
    evfd_ = ::eventfd( 0, EFD_NONBLOCK | EFD_SEMAPHORE );
    assert( evfd_ >= 0 && "eventfd() failed" );
}

fast_eventfd_mutex::~fast_eventfd_mutex()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void fast_eventfd_mutex::unlock() noexcept
{
    // Clear the lock bit and check if any waiters were registered.
    // If prev > 1 (waiter count > 0), write a token to wake one waiter.
    // If prev == 1 (no waiters), skip the kernel call — fast-path optimization.
    uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );

    if ( prev > 1 ) {
        const uint64_t one = 1;
        detail::write_intr( evfd_, &one, sizeof( one ) );
    }
}

void fast_eventfd_mutex::consume_lock() const noexcept
{
    uint64_t val = 0;
    detail::read_intr( evfd_, &val, sizeof( val ) );
}

// Slow path for lock acquisition, entered after try_lock() fails.
//
// Phase 1 — Spin: Attempt CAS in a tight loop with exponential backoff.
//           No waiter registration yet, so unlock() won't touch the kernel.
//
// Phase 2 — Register & wait: Once spinning is exhausted, register as a
//           waiter via async_waiter_guard (add_async_waiter / fetch_add(2))
//           and enter the blocking loop:
//           a) If the lock bit is clear, try an atomic CAS that simultaneously
//              decrements the waiter count and sets the lock bit:
//              desired = (s - 2) | 1.  On success, call consume_lock() to
//              drain any eventfd token unlock() may have written between our
//              registration and this CAS.  Then call guard.dismiss() to avoid
//              double-decrement (the CAS already subtracted 2).
//           b) Otherwise, block on poll() until the eventfd is readable,
//              consume the token, then retry the CAS loop.
void fast_eventfd_mutex::lock_slow() noexcept
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

    // Phase 2: register as a waiter; the guard will call remove_async_waiter()
    // on any exit path where the CAS below doesn't acquire the lock.
    s = add_async_waiter(); // returns state *after* the +2 increment
    detail::async_waiter_guard< fast_eventfd_mutex > guard( *this, detail::adopt_async_waiter );

    while ( true ) {
        if ( ( s & 1u ) == 0 ) {
            // Atomically decrement waiter count and set lock bit in one CAS.
            uint32_t desired = ( s - 2u ) | 1u;
            if ( state_.compare_exchange_weak( s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                // We claimed ownership via the CAS path while registered as a
                // waiter.  Between our poll() return (or initial registration)
                // and this CAS, unlock() may have seen prev > 1 and written an
                // eventfd token.  Drain it so subsequent waiters don't see a
                // spurious wakeup.
                consume_lock();
                guard.dismiss(); // waiter count already decremented in the CAS above
                return;
            }
            continue;
        }

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

bool fast_eventfd_mutex::try_lock_for_ns( duration_type rel ) noexcept
{
    if ( rel <= 0ns )
        return try_lock();

    auto                                             s = add_async_waiter();
    detail::async_waiter_guard< fast_eventfd_mutex > guard( *this, detail::adopt_async_waiter );

    while ( true ) {
        if ( ( s & 1u ) == 0 ) {
            uint32_t desired = ( s - 2u ) | 1u;
            if ( state_.compare_exchange_weak( s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                // We claimed ownership via the CAS path while registered as an
                // async waiter — consume any pending eventfd token to avoid
                // leaving a stray notification for subsequent waiters.
                consume_lock();
                guard.dismiss(); // waiter count already decremented in the CAS above
                return true;
            }
            continue;
        }

        if ( !detail::ppoll_for( evfd_, rel ) ) {
            // Timed out — guard destructor calls remove_async_waiter().
            return false;
        }

        consume_lock();
        s = state_.load( std::memory_order_acquire );
    }
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
