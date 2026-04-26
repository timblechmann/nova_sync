// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/event/native_auto_reset_event.hpp>

#if defined( _WIN32 )
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <poll.h>
#  include <unistd.h>
#  if defined( __linux__ )
#    include <sys/eventfd.h>
#  elif defined( __APPLE__ )
#    include <sys/event.h>
#    include <sys/time.h>
#    include <sys/types.h>
#  endif
#  include <chrono>
#  include <nova/sync/detail/native_handle_support.hpp>
#  include <nova/sync/detail/syscall.hpp>
#  include <nova/sync/detail/timed_wait.hpp>
#endif

namespace nova::sync {

using namespace std::chrono_literals;

// implementation

native_auto_reset_event::native_auto_reset_event( bool initially_set ) noexcept
{
#if defined( _WIN32 )
    handle_.reset( ::CreateEventW( nullptr, FALSE, initially_set ? TRUE : FALSE, nullptr ) );
#elif defined( __linux__ )
    handle_.reset( ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE ) );
    if ( initially_set ) {
        uint64_t val = 1;
        detail::write_intr( handle_.get(), &val, sizeof( val ) );
    }
#elif defined( __APPLE__ )
    handle_.reset( ::kqueue() );
    {
        struct kevent ev;
        // EV_CLEAR: each NOTE_TRIGGER delivers one wakeup and then clears the
        // state.  token_count_ (atomic) provides the actual counting; the kqueue
        // event is used as a pure "something is available" notification.
        EV_SET( &ev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr );
        ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    }
    if ( initially_set ) {
        // Set token and fire the kqueue so async watchers see the fd as readable.
        token_count_.store( 1, std::memory_order_relaxed );
        struct kevent ev;
        EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
        ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    }
#else
    int tmp[ 2 ] { -1, -1 };
    ::pipe( tmp );
    fds_[ 0 ].reset( tmp[ 0 ] );
    fds_[ 1 ].reset( tmp[ 1 ] );
    ::fcntl( fds_[ 0 ].get(), F_SETFL, ::fcntl( fds_[ 0 ].get(), F_GETFL ) | O_NONBLOCK );
    ::fcntl( fds_[ 1 ].get(), F_SETFL, ::fcntl( fds_[ 1 ].get(), F_GETFL ) | O_NONBLOCK );
    if ( initially_set )
        signal();
#endif
}

native_auto_reset_event::~native_auto_reset_event() = default;

#if defined( _WIN32 )
native_auto_reset_event::native_handle_type native_auto_reset_event::native_handle() const noexcept
{
#  if defined( _WIN32 ) || defined( __linux__ ) || defined( __APPLE__ )
    return handle_.get();
#  else
    return fds_[ 0 ].get();
#  endif
}
#endif

void native_auto_reset_event::signal() noexcept
{
#if defined( _WIN32 )
    ::SetEvent( handle_.get() );
#elif defined( __linux__ )
    // Only apply idempotency check if no threads are waiting
    if ( wait_count_.load( std::memory_order_acquire ) == 0 ) {
        struct pollfd pfd = { handle_.get(), POLLIN, 0 };
        if ( detail::poll_intr( &pfd, 1, 0ms ) > 0 ) {
            return; // Already set, don't write
        }
    }
    uint64_t val = 1;
    detail::write_intr( handle_.get(), &val, sizeof( val ) );
#elif defined( __APPLE__ )
    // Strategy:
    //   • token_count_ tracks unconsumed tokens.
    //   • When wait_count_ == 0 (no sync waiters) the event is idempotent:
    //     at most one token is stored. Async event-loop watchers observe the
    //     kqueue fd becoming readable; they call try_wait() to consume the token.
    //   • When wait_count_ > 0 (sync waiters present) we allow counting so that
    //     N signals can wake N threads, even if NOTE_TRIGGER calls coalesce in
    //     the kernel. The first new token fires NOTE_TRIGGER; a waiter that
    //     successfully consumes a token chain-fires the next trigger if tokens
    //     still remain.
    //
    // In both cases we fire NOTE_TRIGGER when a new token is created (old == 0)
    // so that async watchers are always notified.

    int old;
    if ( wait_count_.load( std::memory_order_acquire ) == 0 ) {
        // Idempotent: try to set from 0 → 1.
        int expected = 0;
        if ( !token_count_.compare_exchange_strong( expected, 1, std::memory_order_release, std::memory_order_relaxed ) ) {
            return; // Already ≥ 1; no-op.
        }
        old = 0;    // we just set the first token
    } else {
        // Counting mode: increment unconditionally.
        old = token_count_.fetch_add( 1, std::memory_order_release );
    }

    // Fire NOTE_TRIGGER only for the first new token to avoid double-wakeup.
    if ( old == 0 ) {
        struct kevent ev;
        EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
        ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    }
    // If old > 0, a NOTE_TRIGGER is already in flight or has already woken a
    // waiter.  The chain-wakeup in wait() / try_wait_for() will fire the next
    // trigger once a token is consumed.
#else
    uint8_t dummy = 1;
    detail::write_intr( fds_[ 1 ].get(), &dummy, 1 );
#endif
}

bool native_auto_reset_event::try_wait() noexcept
{
#if defined( _WIN32 )
    return ::WaitForSingleObject( handle_.get(), 0 ) == WAIT_OBJECT_0;
#elif defined( __linux__ )
    uint64_t val;
    return detail::read_intr( handle_.get(), &val, sizeof( val ) ) == static_cast< ssize_t >( sizeof( val ) );
#elif defined( __APPLE__ )
    // Attempt to consume one token via CAS.
    int s = token_count_.load( std::memory_order_acquire );
    while ( s > 0 ) {
        if ( token_count_.compare_exchange_weak( s, s - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return true;
    }
    return false;
#else
    uint8_t buf[ 128 ];
    bool    signaled = false;
    while ( detail::read_intr( fds_[ 0 ].get(), buf, sizeof( buf ) ) > 0 )
        signaled = true;
    return signaled;
#endif
}

void native_auto_reset_event::wait() noexcept
{
#if defined( _WIN32 )
    ::WaitForSingleObject( handle_.get(), INFINITE );
#elif defined( __linux__ )
    wait_count_.fetch_add( 1, std::memory_order_acquire );
    while ( !try_wait() ) {
        struct pollfd pfd = { handle_.get(), POLLIN, 0 };
        detail::poll_intr( &pfd, 1 );
    }
    wait_count_.fetch_sub( 1, std::memory_order_release );
#elif defined( __APPLE__ )
    // Fast path: consume a token without blocking.
    if ( try_wait() )
        return;

    wait_count_.fetch_add( 1, std::memory_order_relaxed );

    while ( true ) {
        // Try again before sleeping — avoids a lost-wakeup if signal() fired
        // between the failed try_wait() above and the fetch_add.
        if ( try_wait() )
            break;

        // Block until a NOTE_TRIGGER arrives.
        struct kevent out {};
        ::kevent( handle_.get(), nullptr, 0, &out, 1, nullptr );

        if ( try_wait() )
            break;
        // Spurious wakeup or another thread raced and took the token — retry.
    }

    // Chain-wakeup: if more tokens remain and other sync waiters are present,
    // fire another NOTE_TRIGGER so the next blocked thread is notified.
    if ( token_count_.load( std::memory_order_acquire ) > 0 && wait_count_.load( std::memory_order_acquire ) > 1 ) {
        struct kevent ev;
        EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
        ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    }

    wait_count_.fetch_sub( 1, std::memory_order_relaxed );
#else
    while ( !try_wait() ) {
        struct pollfd pfd = { native_handle(), POLLIN, 0 };
        detail::poll_intr( &pfd, 1 );
    }
#endif
}

bool native_auto_reset_event::try_wait_for( duration_type timeout ) noexcept
{
#if defined( _WIN32 )
    // Use platform helper which handles retries and rounding.
    return detail::wait_handle_for( handle_.get(), timeout );
#else
    // Non-Windows: use platform-specific timed wait helpers where available.
    if ( timeout <= 0ms )
        return try_wait();

#  if defined( __linux__ )
    if ( detail::ppoll_for( handle_.get(), timeout ) )
        return try_wait();
    return false;
#  elif defined( __APPLE__ )
    // Fast path.
    if ( try_wait() )
        return true;

    wait_count_.fetch_add( 1, std::memory_order_relaxed );

    bool result = false;
    if ( try_wait() ) {
        result = true;
    } else {
        // kevent_for() blocks for up to `timeout`, consuming the NOTE_TRIGGER
        // event when it fires.  After it returns we check the token counter.
        if ( detail::kevent_for( handle_.get(), timeout ) )
            result = try_wait();
        // If still no token (another thread took it), treat as timeout.
    }

    if ( result && token_count_.load( std::memory_order_acquire ) > 0
         && wait_count_.load( std::memory_order_acquire ) > 1 ) {
        // Chain-wakeup for remaining sync waiters.
        struct kevent ev;
        EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
        ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    }

    wait_count_.fetch_sub( 1, std::memory_order_relaxed );
    return result;
#  else
    struct pollfd pfd = { native_handle(), POLLIN, 0 };
    detail::poll_intr( &pfd, 1, timeout );
    return try_wait();
#  endif
#endif
}

} // namespace nova::sync
