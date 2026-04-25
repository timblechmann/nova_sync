// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_srw_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX

#  include <windows.h>

namespace nova::sync {

template < typename LockStorage >
static SRWLOCK* srwlock_ptr( LockStorage& lock ) noexcept
{
    static_assert( sizeof( LockStorage ) == sizeof( SRWLOCK ), "Lock storage size must match SRWLOCK size" );
    return std::launder( reinterpret_cast< SRWLOCK* >( &lock ) );
}


win32_srw_mutex::win32_srw_mutex() noexcept
{
    ::InitializeSRWLock( srwlock_ptr( srwlock_ ) );
}

void win32_srw_mutex::lock() noexcept
{
    ::AcquireSRWLockExclusive( srwlock_ptr( srwlock_ ) );
}

bool win32_srw_mutex::try_lock() noexcept
{
    return ::TryAcquireSRWLockExclusive( srwlock_ptr( srwlock_ ) ) != FALSE;
}

void win32_srw_mutex::unlock() noexcept
{
    ::ReleaseSRWLockExclusive( srwlock_ptr( srwlock_ ) );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SRW_MUTEX
