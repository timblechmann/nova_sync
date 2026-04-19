// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_srw_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>

#    include <cstring>

// SRWLOCK is pointer-sized and must fit in our void* storage field.
static_assert( sizeof( SRWLOCK ) == sizeof( void* ), "SRWLOCK size mismatch with void* storage" );

namespace nova::sync {

static SRWLOCK* srw( void** p ) noexcept
{
    return std::launder( reinterpret_cast< SRWLOCK* >( p ) );
}

win32_srw_mutex::win32_srw_mutex() noexcept
{
    ::InitializeSRWLock( srw( &srwlock_ ) );
}

void win32_srw_mutex::lock() noexcept
{
    ::AcquireSRWLockExclusive( srw( &srwlock_ ) );
}

bool win32_srw_mutex::try_lock() noexcept
{
    return ::TryAcquireSRWLockExclusive( srw( &srwlock_ ) ) != FALSE;
}

void win32_srw_mutex::unlock() noexcept
{
    ::ReleaseSRWLockExclusive( srw( &srwlock_ ) );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SRW_MUTEX
