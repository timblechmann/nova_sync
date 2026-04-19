// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/async_concepts.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED )

#    include <cassert>
#    include <mutex>
#    include <system_error>

#    if defined( __linux__ ) || defined( __APPLE__ )
#        include <unistd.h>
#    endif

namespace nova::sync::detail {

// ---------------------------------------------------------------------------
// RAII wrapper for file descriptors (POSIX only)
// ---------------------------------------------------------------------------

#    if defined( __linux__ ) || defined( __APPLE__ )
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
#    endif // __linux__ || __APPLE__

/// @brief Invoke a handler with a successful expected<unique_lock, error_code> result.
///
/// Dispatches to the appropriate expected implementation (std::expected or
/// tl::expected) based on compile-time availability. Asserts if the handler
/// cannot be invoked with the expected type.
template < typename Mutex, typename Handler >
void invoke_with_lock( Handler&& handler, std::unique_lock< Mutex > lock )
{
#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type { std::move( lock ) } );
#    endif
#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type { std::move( lock ) } );
#    endif

    assert( false && "Handler must be invocable with expected<unique_lock, error_code>" );
#    ifdef __cpp_lib_unreachable
    std::unreachable();
#    endif
}

/// @brief Invoke a handler with an error expected<unique_lock, error_code> result.
///
/// Dispatches to the appropriate expected implementation (std::expected or
/// tl::expected) based on compile-time availability. Asserts if the handler
/// cannot be invoked with the expected type.
template < typename Mutex, typename Handler >
void invoke_with_error( Handler&& handler, std::error_code ec )
{
#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type { std::unexpect, ec } );
#    endif
#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< std::unique_lock< Mutex >, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type { tl::unexpect, ec } );
#    endif

    assert( false && "Handler must be invocable with expected<unique_lock, error_code>" );
#    ifdef __cpp_lib_unreachable
    std::unreachable();
#    endif
}

template < typename Mutex, typename Handler >
void invoke_with_error( Handler&& handler, std::errc ec )
{
    invoke_with_error< Mutex, Handler >( std::forward< Handler >( handler ), std::make_error_code( ec ) );
}

inline bool platform_try_acquire_after_wait( auto& mtx )
{
#    if defined( __linux__ ) || defined( __APPLE__ )
    // POSIX: must try to acquire, otherwise we have a suprious wakeup
    return mtx.try_lock();
#    elif defined( _WIN32 )
    // Windows: ownership is already held; just verify no error
    // The ec parameter comes from the async_wait completion; if non-zero,
    // the completion failed (e.g., WAIT_ABANDONED).
    return true; // Already own the lock
#    endif
}


} // namespace nova::sync::detail

#endif // defined( NOVA_SYNC_HAS_EXPECTED )
