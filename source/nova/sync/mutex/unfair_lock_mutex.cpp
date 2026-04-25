// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/unfair_lock_mutex.hpp>

#ifdef NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX

#  include <os/lock.h>

#  include <cstring>

static_assert( sizeof( os_unfair_lock ) == sizeof( unsigned int ),
               "os_unfair_lock size mismatch with unsigned int storage" );
static_assert( alignof( os_unfair_lock ) <= alignof( unsigned int ),
               "os_unfair_lock alignment requirement exceeds unsigned int" );

namespace nova::sync {

apple_os_unfair_mutex::apple_os_unfair_mutex() noexcept
{
    *lk( &lock_ ) = OS_UNFAIR_LOCK_INIT;
}

void apple_os_unfair_mutex::lock() noexcept
{
    os_unfair_lock_lock( lk( &lock_ ) );
}

bool apple_os_unfair_mutex::try_lock() noexcept
{
    return os_unfair_lock_trylock( lk( &lock_ ) );
}

void apple_os_unfair_mutex::unlock() noexcept
{
    os_unfair_lock_unlock( lk( &lock_ ) );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX
