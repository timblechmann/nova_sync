// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/semaphore/eventfd_semaphore.hpp>

#ifdef NOVA_SYNC_HAS_EVENTFD_SEMAPHORE

#  include <nova/sync/detail/syscall.hpp>

#  include <poll.h>
#  include <sys/eventfd.h>
#  include <unistd.h>

#  include <cassert>

namespace nova::sync {

eventfd_semaphore::eventfd_semaphore( std::ptrdiff_t initial ) :
    evfd_ { ::eventfd( unsigned( initial ), EFD_NONBLOCK | EFD_SEMAPHORE ) }
{
    assert( evfd_ >= 0 && "eventfd() failed" );
}

eventfd_semaphore::~eventfd_semaphore()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void eventfd_semaphore::release( std::ptrdiff_t n ) noexcept
{
    assert( n >= 0 && "eventfd_semaphore::release: n must be non-negative" );
    const uint64_t val = uint64_t( n );
    detail::write_intr( evfd_, &val, sizeof( val ) );
}

void eventfd_semaphore::acquire() noexcept
{
    while ( !try_acquire() ) {
        struct pollfd pfd {
            evfd_,
            POLLIN,
            0,
        };
        detail::poll_intr( &pfd, 1 );
    }
}

bool eventfd_semaphore::try_acquire() noexcept
{
    uint64_t val = 0;
    return detail::read_intr( evfd_, &val, sizeof( val ) ) == sizeof( val );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_SEMAPHORE
