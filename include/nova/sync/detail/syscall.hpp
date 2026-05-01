// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <span>

#if __has_include( <unistd.h> ) && __has_include( <poll.h> ) && __has_include( <errno.h> )
#  include <cstddef>
#  include <errno.h>
#  include <poll.h>
#  include <sys/types.h>
#  include <unistd.h>

namespace nova::sync::detail {

// Retry a POSIX read() across EINTR. Returns the same return value as read().
inline ssize_t read_intr( int fd, std::span< std::byte > buf ) noexcept
{
    ssize_t rc;
    do {
        rc = ::read( fd, buf.data(), buf.size() );
    } while ( rc < 0 && errno == EINTR );
    return rc;
}

inline ssize_t write_intr( int fd, std::span< const std::byte > buf ) noexcept
{
    const std::byte* p         = buf.data();
    size_t           remaining = buf.size();
    size_t           offset    = 0;

    while ( remaining > 0 ) {
        ssize_t rc = ::write( fd, p + offset, remaining );
        if ( rc < 0 ) {
            if ( errno == EINTR )
                continue; // try again
            return -1;    // propagate error
        }
        // advance
        remaining -= rc;
        offset += rc;
    }

    return ssize_t( buf.size() );
}

inline int poll_intr( std::span< struct pollfd > fds ) noexcept
{
    int rc;
    do {
        rc = ::poll( fds.data(), nfds_t( fds.size() ), -1 );
    } while ( rc < 0 && errno == EINTR );
    return rc;
}

inline int poll_intr( struct pollfd& fd ) noexcept
{
    return poll_intr( std::span< struct pollfd >( &fd, 1 ) );
}


inline int try_poll( std::span< struct pollfd > fds ) noexcept
{
    return ::poll( fds.data(), nfds_t( fds.size() ), 0 );
}

inline int try_poll( struct pollfd& fd ) noexcept
{
    return try_poll( std::span< struct pollfd >( &fd, 1 ) );
}

inline int poll_intr( std::span< struct pollfd > fds, std::chrono::milliseconds timeout ) noexcept
{
    if ( timeout <= std::chrono::milliseconds::zero() )
        return try_poll( fds );

    using clock      = std::chrono::steady_clock;
    const auto start = clock::now();

    while ( true ) {
        int rem = int( std::chrono::duration_cast< std::chrono::milliseconds >( timeout ).count() );
        int rc  = ::poll( fds.data(), nfds_t( fds.size() ), rem );
        if ( rc < 0 ) {
            if ( errno == EINTR ) {
                timeout -= std::chrono::duration_cast< std::chrono::milliseconds >( clock::now() - start );
                if ( timeout <= std::chrono::milliseconds::zero() )
                    return 0; // non-blocking poll interrupted, treat as timeout
                continue;     // recompute remaining and retry
            }
        }
        return rc;
    }
}

inline int poll_intr( struct pollfd& fd, std::chrono::milliseconds timeout ) noexcept
{
    return poll_intr( std::span< struct pollfd >( &fd, 1 ), timeout );
}

} // namespace nova::sync::detail

#endif

namespace nova::sync::detail {

#if defined( __linux__ ) || defined( __APPLE__ )
int duplicate_native_handle( int fd ) noexcept;
#elif defined( _WIN32 )
void* duplicate_native_handle( void* handle ) noexcept;
#endif

} // namespace nova::sync::detail
