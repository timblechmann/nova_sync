// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/event/native_manual_reset_event.hpp>

#include <nova/sync/detail/timed_wait.hpp>

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
#  include <nova/sync/detail/syscall.hpp>
#endif

namespace nova::sync {

using namespace std::chrono_literals;

native_manual_reset_event::native_manual_reset_event( bool initially_set ) noexcept
{
#if defined( _WIN32 )
    handle_.reset( ::CreateEventW( nullptr, TRUE, initially_set ? TRUE : FALSE, nullptr ) );
#elif defined( __linux__ )
    handle_.reset( ::eventfd( initially_set ? 1 : 0, EFD_CLOEXEC | EFD_NONBLOCK ) );
#elif defined( __APPLE__ )
    handle_.reset( ::kqueue() );
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, EV_ADD, 0, 0, nullptr );
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

native_manual_reset_event::~native_manual_reset_event() = default;

native_manual_reset_event::native_handle_type native_manual_reset_event::native_handle() const noexcept
{
#if defined( _WIN32 ) || defined( __linux__ ) || defined( __APPLE__ )
    return handle_.get();
#else
    return fds_[ 0 ].get();
#endif
}

void native_manual_reset_event::signal() noexcept
{
#if defined( _WIN32 )
    ::SetEvent( handle_.get() );
#elif defined( __linux__ )
    struct pollfd pfd = { handle_.get(), POLLIN, 0 };
    if ( detail::poll_intr( &pfd, 1, 0ms ) == 0 ) {
        uint64_t val = 1;
        detail::write_intr( handle_.get(), &val, sizeof( val ) );
    }
#elif defined( __APPLE__ )
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
    ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
#else
    struct pollfd pfd = { fds_[ 0 ].get(), POLLIN, 0 };
    if ( detail::poll_intr( &pfd, 1, 0ms ) == 0 ) {
        uint8_t dummy = 1;
        detail::write_intr( fds_[ 1 ].get(), &dummy, 1 );
    }
#endif
}

void native_manual_reset_event::reset() noexcept
{
#if defined( _WIN32 )
    ::ResetEvent( handle_.get() );
#elif defined( __linux__ )
    uint64_t val;
    while ( detail::read_intr( handle_.get(), &val, sizeof( val ) ) == ssize_t( sizeof( val ) ) ) {}
#elif defined( __APPLE__ )
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, EV_DELETE, 0, 0, nullptr );
    ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
    EV_SET( &ev, 1, EVFILT_USER, EV_ADD, 0, 0, nullptr );
    ::kevent( handle_.get(), &ev, 1, nullptr, 0, nullptr );
#else
    uint8_t buf[ 128 ];
    while ( detail::read_intr( fds_[ 0 ].get(), buf, sizeof( buf ) ) > 0 ) {}
#endif
}

bool native_manual_reset_event::try_wait() const noexcept
{
#if defined( _WIN32 )
    return ::WaitForSingleObject( handle_.get(), 0 ) == WAIT_OBJECT_0;
#else
    struct pollfd pfd = { native_handle(), POLLIN, 0 };
    int           rc  = detail::poll_intr( &pfd, 1, 0ms );
    return rc > 0;
#endif
}

void native_manual_reset_event::wait() const noexcept
{
#if defined( _WIN32 )
    ::WaitForSingleObject( handle_.get(), INFINITE );
#else
    while ( !try_wait() ) {
        struct pollfd pfd = { native_handle(), POLLIN, 0 };
        detail::poll_intr( &pfd, 1 );
    }
#endif
}

bool native_manual_reset_event::try_wait_for( duration_type timeout ) const noexcept
{
#if defined( _WIN32 )
    return detail::wait_handle_for( handle_.get(), timeout );
#else
    if ( timeout <= 0ms )
        return try_wait();

#  if defined( __linux__ )
    if ( detail::ppoll_for( native_handle(), timeout ) )
        return try_wait();
    return false;
#  elif defined( __APPLE__ )
    if ( detail::kevent_for( native_handle(), timeout ) )
        return try_wait();
    return false;
#  else
    struct pollfd pfd = { native_handle(), POLLIN, 0 };
    int           rc  = detail::poll_intr( &pfd, 1, timeout );
    if ( rc <= 0 )
        return try_wait();
    return try_wait();
#  endif
#endif
}

} // namespace nova::sync
