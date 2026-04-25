// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __has_include( <unistd.h> )
#  include <unistd.h>
#endif

#if defined( _WIN32 )
#  include <limits>
typedef void* HANDLE;
extern "C" {
__declspec( dllimport ) int __stdcall CloseHandle( void* );
}

#  include <memory>

#endif

namespace nova::sync::detail {

#if __has_include( <unistd.h> )

class scoped_file_descriptor
{
public:
    scoped_file_descriptor() noexcept = default;

    explicit scoped_file_descriptor( int fd ) noexcept :
        fd_( fd )
    {}

    // Construct by duplicating an existing fd
    static scoped_file_descriptor from_dup( int source_fd ) noexcept
    {
        if ( source_fd < 0 )
            return scoped_file_descriptor( -1 );
        int dup_fd = ::dup( source_fd );
        return scoped_file_descriptor( dup_fd );
    }

    ~scoped_file_descriptor() noexcept
    {
        if ( fd_ >= 0 )
            ::close( fd_ );
    }

    // Non-copyable, moveable
    scoped_file_descriptor( const scoped_file_descriptor& )            = delete;
    scoped_file_descriptor& operator=( const scoped_file_descriptor& ) = delete;

    scoped_file_descriptor( scoped_file_descriptor&& other ) noexcept :
        fd_( other.release() )
    {}
    scoped_file_descriptor& operator=( scoped_file_descriptor&& other ) noexcept
    {
        reset( other.release() );
        return *this;
    }

    int get() const noexcept
    {
        return fd_;
    }

    int release() noexcept
    {
        int tmp = fd_;
        fd_     = -1;
        return tmp;
    }

    void reset( int fd = -1 ) noexcept
    {
        if ( fd_ >= 0 )
            ::close( fd_ );
        fd_ = fd;
    }

    explicit operator bool() const noexcept
    {
        return fd_ >= 0;
    }

private:
    int fd_ = -1;
};

#endif // __has_include( <unistd.h> )

// ---------------------------------------------------------------------------
// RAII wrapper for Windows HANDLE
// ---------------------------------------------------------------------------

#if defined( _WIN32 )

struct handle_deleter
{
    void operator()( HANDLE h ) const noexcept
    {
        const HANDLE invalidHandle = HANDLE( std::numeric_limits< uintptr_t >::max() );
        if ( h && h != invalidHandle )
            ::CloseHandle( h );
    }
};

using scoped_handle = std::unique_ptr< void, handle_deleter >;

#endif // _WIN32

} // namespace nova::sync::detail
