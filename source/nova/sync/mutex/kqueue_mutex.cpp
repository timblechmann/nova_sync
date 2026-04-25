// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/kqueue_mutex.hpp>

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX

#  include <nova/sync/detail/backoff.hpp>
#  include <nova/sync/detail/timed_wait.hpp>
#  include <nova/sync/mutex/support/async_waiter_guard.hpp>

#  include <sys/event.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>

#  include <cassert>
#  include <cerrno>

namespace nova::sync {

using namespace std::chrono_literals;

static constexpr uintptr_t kqueue_ident = 1;

// ---------------------------------------------------------------------------
// kqueue_mutex — simple kevent-based variant, no user-space waiter count
// ---------------------------------------------------------------------------

kqueue_mutex::kqueue_mutex() :
    kqfd_ {
        ::kqueue(),
    }
{
    assert( kqfd_ >= 0 && "kqueue() failed" );

    struct kevent kev {};
    EV_SET( &kev, kqueue_ident, EVFILT_USER, EV_ADD | EV_CLEAR, NOTE_TRIGGER, 0, nullptr );
    [[maybe_unused]] int r = ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
    assert( r == 0 && "kevent EV_ADD EVFILT_USER failed" );
}

kqueue_mutex::~kqueue_mutex()
{
    if ( kqfd_ >= 0 )
        ::close( kqfd_ );
}

void kqueue_mutex::lock() noexcept
{
    struct kevent out {};
    ::kevent( kqfd_, nullptr, 0, &out, 1, nullptr );
}

bool kqueue_mutex::try_lock() noexcept
{
    struct kevent   out {};
    struct timespec ts { 0, 0 };
    return ::kevent( kqfd_, nullptr, 0, &out, 1, &ts ) > 0;
}

void kqueue_mutex::unlock() noexcept
{
    struct kevent kev {};
    EV_SET( &kev, kqueue_ident, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
    ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
}

// ---------------------------------------------------------------------------
// fast_kqueue_mutex — fast-path variant with user-space waiter count
// ---------------------------------------------------------------------------

static constexpr uintptr_t fast_kqueue_ident = 2;

fast_kqueue_mutex::fast_kqueue_mutex() :
    kqfd_ {
        ::kqueue(),
    }
{
    assert( kqfd_ >= 0 && "kqueue() failed" );

    struct kevent kev {};
    EV_SET( &kev, fast_kqueue_ident, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr );
    [[maybe_unused]] int r = ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
    assert( r == 0 && "kevent EV_ADD EVFILT_USER failed" );
}

fast_kqueue_mutex::~fast_kqueue_mutex()
{
    if ( kqfd_ >= 0 )
        ::close( kqfd_ );
}

void fast_kqueue_mutex::unlock() noexcept
{
    // Clear the lock bit and check if any waiters were registered.
    // If prev > 1 (waiter count > 0), post NOTE_TRIGGER to wake one waiter.
    // If prev == 1 (no waiters), skip the kernel call — fast-path optimization.
    uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );

    if ( prev > 1 ) {
        struct kevent kev {};
        EV_SET( &kev, fast_kqueue_ident, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
        ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
    }
}

void fast_kqueue_mutex::consume_lock() const noexcept
{
    struct kevent   out {};
    struct timespec ts { 0, 0 };
    ::kevent( kqfd_, nullptr, 0, &out, 1, &ts );
}

void fast_kqueue_mutex::lock_slow() noexcept
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
    detail::async_waiter_guard< fast_kqueue_mutex > guard( *this, detail::adopt_async_waiter );

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

        struct kevent out {};
        ::kevent( kqfd_, nullptr, 0, &out, 1, nullptr );
        s = state_.load( std::memory_order_relaxed );
    }
}

bool fast_kqueue_mutex::try_lock_for_impl( std::chrono::nanoseconds rel ) noexcept
{
    if ( rel <= 0ns )
        return try_lock();

    uint32_t expected = 0;
    if ( state_.compare_exchange_weak( expected, 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
        return true;

    auto                                            s = add_async_waiter();
    detail::async_waiter_guard< fast_kqueue_mutex > guard( *this, detail::adopt_async_waiter );

    while ( true ) {
        if ( ( s & 1u ) == 0 ) {
            uint32_t desired = ( s - 2u ) | 1u;
            if ( state_.compare_exchange_weak( s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                consume_lock();
                guard.release();
                return true;
            }
            continue;
        }

        if ( !detail::kevent_for( kqfd_, rel ) )
            // Timed out — guard destructor calls remove_async_waiter().
            return false;


        consume_lock();
        s = state_.load( std::memory_order_acquire );
    }
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_MUTEX
