// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/eventfd_mutex.hpp>

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX

#    include <nova/sync/detail/syscall.hpp>

#    include <poll.h>
#    include <sys/eventfd.h>
#    include <unistd.h>

#    include <cassert>

namespace nova::sync {

eventfd_mutex::eventfd_mutex() :
    evfd_ { ::eventfd( 1, EFD_NONBLOCK | EFD_SEMAPHORE ) }
{
    assert( evfd_ >= 0 && "eventfd() failed" );
}

eventfd_mutex::~eventfd_mutex()
{
    if ( evfd_ >= 0 )
        ::close( evfd_ );
}

void eventfd_mutex::lock() noexcept
{
    while ( !try_lock() ) {
        struct pollfd pfd {
            evfd_,
            POLLIN,
            0,
        };
        detail::poll_intr( &pfd, 1 );
    }
}

bool eventfd_mutex::try_lock() noexcept
{
    uint64_t val = 0;
    return detail::read_intr( evfd_, &val, sizeof( val ) ) == sizeof( val );
}

void eventfd_mutex::unlock() noexcept
{
    const uint64_t one = 1;
    detail::write_intr( evfd_, &one, sizeof( one ) );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
