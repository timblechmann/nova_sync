// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/native_auto_reset_event.hpp>

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
#endif

namespace nova::sync {

native_auto_reset_event::native_auto_reset_event( bool initially_set ) noexcept
{
#if defined( _WIN32 )
    handle_ = ::CreateEventW( nullptr, FALSE, initially_set ? TRUE : FALSE, nullptr );
#elif defined( __linux__ )
    handle_ = ::eventfd( initially_set ? 1 : 0, EFD_CLOEXEC | EFD_NONBLOCK );
#elif defined( __APPLE__ )
    handle_ = ::kqueue();
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr );
    ::kevent( handle_, &ev, 1, nullptr, 0, nullptr );
    if ( initially_set )
        signal();
#else
    ::pipe( fds_ );
    ::fcntl( fds_[ 0 ], F_SETFL, ::fcntl( fds_[ 0 ], F_GETFL ) | O_NONBLOCK );
    ::fcntl( fds_[ 1 ], F_SETFL, ::fcntl( fds_[ 1 ], F_GETFL ) | O_NONBLOCK );
    if ( initially_set )
        signal();
#endif
}

native_auto_reset_event::~native_auto_reset_event()
{
#if defined( _WIN32 )
    ::CloseHandle( static_cast< HANDLE >( handle_ ) );
#elif defined( __linux__ ) || defined( __APPLE__ )
    ::close( handle_ );
#else
    ::close( fds_[ 0 ] );
    ::close( fds_[ 1 ] );
#endif
}

native_auto_reset_event::native_handle_type native_auto_reset_event::native_handle() const noexcept
{
#if defined( _WIN32 ) || defined( __linux__ ) || defined( __APPLE__ )
    return handle_;
#else
    return fds_[ 0 ];
#endif
}

void native_auto_reset_event::signal() noexcept
{
#if defined( _WIN32 )
    ::SetEvent( static_cast< HANDLE >( handle_ ) );
#elif defined( __linux__ )
    uint64_t val = 1;
    ::write( handle_, &val, sizeof( val ) );
#elif defined( __APPLE__ )
    struct kevent ev;
    EV_SET( &ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
    ::kevent( handle_, &ev, 1, nullptr, 0, nullptr );
#else
    uint8_t dummy = 1;
    ::write( fds_[ 1 ], &dummy, 1 );
#endif
}

bool native_auto_reset_event::try_wait() noexcept
{
#if defined( _WIN32 )
    return ::WaitForSingleObject( static_cast< HANDLE >( handle_ ), 0 ) == WAIT_OBJECT_0;
#elif defined( __linux__ )
    uint64_t val;
    return ::read( handle_, &val, sizeof( val ) ) == sizeof( val );
#elif defined( __APPLE__ )
    struct timespec ts = { 0, 0 };
    struct kevent   ev;
    return ::kevent( handle_, nullptr, 0, &ev, 1, &ts ) > 0;
#else
    uint8_t buf[ 128 ];
    bool    signaled = false;
    while ( ::read( fds_[ 0 ], buf, sizeof( buf ) ) > 0 )
        signaled = true;
    return signaled;
#endif
}

void native_auto_reset_event::wait() noexcept
{
#if defined( _WIN32 )
    ::WaitForSingleObject( static_cast< HANDLE >( handle_ ), INFINITE );
#else
    while ( !try_wait() ) {
        struct pollfd pfd = { native_handle(), POLLIN, 0 };
        ::poll( &pfd, 1, -1 );
    }
#endif
}

bool native_auto_reset_event::wait_for_impl( long long timeout_ms ) noexcept
{
    if ( timeout_ms < 0 )
        timeout_ms = 0;

#if defined( _WIN32 )
    return ::WaitForSingleObject( static_cast< HANDLE >( handle_ ), static_cast< DWORD >( timeout_ms ) )
           == WAIT_OBJECT_0;
#else
    using clock_t = std::chrono::steady_clock;
    auto end_time = clock_t::now() + std::chrono::milliseconds( timeout_ms );

    while ( !try_wait() ) {
        auto now = clock_t::now();
        if ( now >= end_time ) {
            return try_wait();
        }

        long long     remaining_ms = std::chrono::duration_cast< std::chrono::milliseconds >( end_time - now ).count();
        struct pollfd pfd          = { native_handle(), POLLIN, 0 };
        ::poll( &pfd, 1, static_cast< int >( remaining_ms ) );
    }
    return true;
#endif
}

} // namespace nova::sync
