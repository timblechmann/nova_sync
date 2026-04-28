// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/semaphore/mach_semaphore.hpp>

#ifdef NOVA_SYNC_HAS_MACH_SEMAPHORE

#  include <mach/mach_init.h>
#  include <mach/mach_traps.h>
#  include <mach/task.h>

#  include <cassert>

namespace nova::sync {

mach_semaphore::mach_semaphore( std::ptrdiff_t initial )
{
    kern_return_t result = semaphore_create( mach_task_self(), &sem_, SYNC_POLICY_FIFO, int( initial ) );
    assert( result == KERN_SUCCESS && "semaphore_create failed" );
    (void)result;
}

mach_semaphore::~mach_semaphore()
{
    semaphore_destroy( mach_task_self(), sem_ );
}

void mach_semaphore::release( std::ptrdiff_t n ) noexcept
{
    assert( n >= 0 && "mach_semaphore::release: n must be non-negative" );
    for ( std::ptrdiff_t i = 0; i < n; ++i )
        semaphore_signal( sem_ );
}

void mach_semaphore::acquire() noexcept
{
    while ( true ) {
        kern_return_t result = semaphore_wait( sem_ );
        if ( result == KERN_SUCCESS )
            return;
        if ( result == KERN_ABORTED )
            continue; // spurious wakeup (e.g., debugger)
        assert( false && "semaphore_wait failed" );
        return;
    }
}

bool mach_semaphore::try_acquire() noexcept
{
    const mach_timespec_t zero = { 0, 0 };
    return timed_wait( zero );
}

bool mach_semaphore::timed_wait( const mach_timespec_t& wait_time ) noexcept
{
    kern_return_t result = semaphore_timedwait( sem_, wait_time );
    if ( result == KERN_SUCCESS )
        return true;
    if ( result == KERN_OPERATION_TIMED_OUT )
        return false;
    assert( false && "semaphore_timedwait failed" );
    return false;
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_MACH_SEMAPHORE
