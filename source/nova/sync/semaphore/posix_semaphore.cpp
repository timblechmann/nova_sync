// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/semaphore/posix_semaphore.hpp>

#ifdef NOVA_SYNC_HAS_POSIX_SEMAPHORE

#  include <cassert>
#  include <cerrno>

namespace nova::sync {

posix_semaphore::posix_semaphore( std::ptrdiff_t initial )
{
    int r = ::sem_init( &sem_, 0, unsigned( initial ) );
    assert( r == 0 && "sem_init failed" );
    (void)r;
}

posix_semaphore::~posix_semaphore()
{
    ::sem_destroy( &sem_ );
}

void posix_semaphore::release( std::ptrdiff_t n ) noexcept
{
    assert( n >= 0 && "posix_semaphore::release: n must be non-negative" );
    for ( std::ptrdiff_t i = 0; i < n; ++i )
        ::sem_post( &sem_ );
}

void posix_semaphore::acquire() noexcept
{
    while ( true ) {
        if ( ::sem_wait( &sem_ ) == 0 )
            return;
        if ( errno == EINTR )
            continue;
        assert( false && "sem_wait failed" );
        return;
    }
}

bool posix_semaphore::try_acquire() noexcept
{
    if ( ::sem_trywait( &sem_ ) == 0 )
        return true;
    return false;
}

bool posix_semaphore::try_acquire_until_system(
    std::chrono::time_point< std::chrono::system_clock, std::chrono::system_clock::duration > abs_time ) noexcept
{
    auto secs = std::chrono::time_point_cast< std::chrono::seconds >( abs_time );
    auto ns   = std::chrono::duration_cast< std::chrono::nanoseconds >( abs_time - secs );

    struct timespec ts;
    ts.tv_sec  = secs.time_since_epoch().count();
    ts.tv_nsec = ns.count();

    while ( true ) {
        if ( ::sem_timedwait( &sem_, &ts ) == 0 )
            return true;

        switch ( errno ) {
        case EINTR:     continue;
        case ETIMEDOUT: return false;
        default:        assert( false && "sem_timedwait failed" ); return false;
        }
    }
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_POSIX_SEMAPHORE
