// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>

#if __has_include( <unistd.h> ) && __has_include( <poll.h> ) && __has_include( <errno.h> )
#  include <cstddef>
#  include <errno.h>
#  include <poll.h>
#  include <sys/types.h>
#  include <unistd.h>

namespace nova::sync::detail {

// Retry a POSIX read() across EINTR. Returns the same return value as read().
inline ssize_t read_intr( int fd, void* buf, size_t count ) noexcept
{
    ssize_t rc;
    do {
        rc = ::read( fd, buf, count );
    } while ( rc < 0 && errno == EINTR );
    return rc;
}

inline ssize_t write_intr( int fd, const void* buf, size_t count ) noexcept
{
    const unsigned char* p         = static_cast< const unsigned char* >( buf );
    size_t               remaining = count;
    size_t               offset    = 0;

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

    return static_cast< ssize_t >( count );
}
inline int poll_intr( struct pollfd* fds, nfds_t nfds ) noexcept
{
    int rc;
    do {
        rc = ::poll( fds, nfds, -1 );
    } while ( rc < 0 && errno == EINTR );
    return rc;
}

inline int poll_intr( struct pollfd* fds, nfds_t nfds, std::chrono::milliseconds timeout ) noexcept
{
    using clock = std::chrono::steady_clock;
    auto start  = clock::now();

    while ( true ) {
        int rem = static_cast< int >( std::chrono::duration_cast< std::chrono::milliseconds >( timeout ).count() );
        int rc  = ::poll( fds, nfds, rem );
        if ( rc < 0 ) {
            if ( errno == EINTR ) {
                auto elapsed = clock::now() - start;
                if ( elapsed >= timeout )
                    return 0; // timed out
                timeout -= std::chrono::duration_cast< std::chrono::milliseconds >( elapsed );
                continue;
            }
        }
        return rc;
    }
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
