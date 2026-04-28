// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/semaphore/kqueue_semaphore.hpp>

#ifdef NOVA_SYNC_HAS_KQUEUE_SEMAPHORE

#  include <nova/sync/detail/timed_wait.hpp>

#  include <sys/event.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>

#  include <cassert>

namespace nova::sync {

using namespace std::chrono_literals;

kqueue_semaphore::kqueue_semaphore( std::ptrdiff_t initial ) :
    kqfd_ { ::kqueue() },
    count_( int32_t( initial ) )
{
    assert( kqfd_ >= 0 && "kqueue() failed" );

    struct kevent kev {};
    EV_SET( &kev, kqueue_ident_, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr );
    [[maybe_unused]] int r = ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
    assert( r == 0 && "kevent EV_ADD EVFILT_USER failed" );
}

kqueue_semaphore::~kqueue_semaphore()
{
    if ( kqfd_ >= 0 )
        ::close( kqfd_ );
}

void kqueue_semaphore::notify() noexcept
{
    struct kevent kev {};
    EV_SET( &kev, kqueue_ident_, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
    ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
}

void kqueue_semaphore::release( std::ptrdiff_t n ) noexcept
{
    assert( n >= 0 && "kqueue_semaphore::release: n must be non-negative" );
    auto prev = count_.fetch_add( int32_t( n ), std::memory_order_release );
    if ( prev < 0 ) {
        // Wake blocked (synchronous) waiters
        auto to_wake = std::min( int32_t( n ), -prev );
        for ( int32_t i = 0; i < to_wake; ++i )
            notify();
    } else {
        // Wake async waiters polling via try_acquire() on the kqueue fd
        notify();
    }
}

void kqueue_semaphore::acquire() noexcept
{
    auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
    if ( prev > 0 )
        return;

    // Block until a release() sends us a notification.
    // Our token was already claimed by the fetch_sub above.
    // Because EVFILT_USER with EV_CLEAR coalesces multiple triggers,
    // we re-notify after waking so that other blocked waiters proceed.
    while ( true ) {
        struct kevent out {};
        int           rc = ::kevent( kqfd_, nullptr, 0, &out, 1, nullptr );
        if ( rc > 0 ) {
            notify();
            return;
        }
        // rc < 0 && errno == EINTR: retry
    }
}

bool kqueue_semaphore::try_acquire() noexcept
{
    auto c = count_.load( std::memory_order_relaxed );
    while ( c > 0 ) {
        if ( count_.compare_exchange_weak( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return true;
    }
    return false;
}

bool kqueue_semaphore::try_acquire_for( duration_type rel_ns ) noexcept
{
    if ( rel_ns.count() <= 0 )
        return try_acquire();

    if ( try_acquire() )
        return true;

    auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
    if ( prev > 0 )
        return true;

    // Wait for notification. Token already claimed by fetch_sub.
    if ( detail::kevent_for( kqfd_, rel_ns ) )
        return true;

    // Timed out — undo the fetch_sub
    auto c = count_.fetch_add( 1, std::memory_order_release );
    if ( c >= 0 ) {
        // A release()'s notify() is in flight for us — consume it and succeed
        struct kevent   out {};
        struct timespec ts { 0, 0 };
        ::kevent( kqfd_, nullptr, 0, &out, 1, &ts );
        return true;
    }
    return false;
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_SEMAPHORE
