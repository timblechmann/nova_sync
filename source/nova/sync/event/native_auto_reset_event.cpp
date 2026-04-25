// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/event/native_auto_reset_event.hpp>

#if defined( _WIN32 )
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <poll.h>
#    include <unistd.h>
#    if defined( __linux__ )
#        include <sys/eventfd.h>
#    elif defined( __APPLE__ )
#        include <sys/event.h>
#        include <sys/time.h>
#        include <sys/types.h>
#    endif
#    include <chrono>
#    include <nova/sync/detail/native_handle_support.hpp>
#    include <nova/sync/detail/syscall.hpp>
#    include <nova/sync/detail/timed_wait.hpp>
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
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr );
    ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    if ( initially_set )
        signal();
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

#if defined( _WIN32 ) || defined( __APPLE__ )
native_auto_reset_event::native_handle_type native_auto_reset_event::native_handle() const noexcept
{
#    if defined( _WIN32 ) || defined( __linux__ ) || defined( __APPLE__ )
    return handle_.get();
#    else
    return fds_[ 0 ].get();
#    endif
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
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
    ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
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
    struct timespec ts = { 0, 0 };
    struct kevent   ev;
    return ::kevent( handle_.get(), nullptr, 0, &ev, 1, &ts ) > 0;
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

#    if defined( __linux__ )
    if ( detail::ppoll_for( handle_.get(), timeout ) )
        return try_wait();
    return false;
#    elif defined( __APPLE__ )
    if ( detail::kevent_for( native_handle(), timeout ) )
        return try_wait();
    return false;
#    else
    struct pollfd pfd = { native_handle(), POLLIN, 0 };
    detail::poll_intr( &pfd, 1, timeout );
    return try_wait();
#    endif
#endif
}

} // namespace nova::sync
