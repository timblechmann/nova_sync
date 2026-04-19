// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>

#    include <cassert>

namespace nova::sync {

win32_mutex::win32_mutex()
{
    handle_ = ::CreateMutexW( nullptr, FALSE, nullptr );
    assert( handle_ != nullptr && "CreateMutex failed" );
}

win32_mutex::~win32_mutex()
{
    if ( handle_ )
        ::CloseHandle( static_cast< HANDLE >( handle_ ) );
}

void win32_mutex::lock() noexcept
{
    ::WaitForSingleObject( static_cast< HANDLE >( handle_ ), INFINITE );
}

bool win32_mutex::try_lock() noexcept
{
    return ::WaitForSingleObject( static_cast< HANDLE >( handle_ ), 0 ) == WAIT_OBJECT_0;
}

bool win32_mutex::try_lock_ms( unsigned long timeout_ms ) noexcept
{
    return ::WaitForSingleObject( static_cast< HANDLE >( handle_ ), timeout_ms ) == WAIT_OBJECT_0;
}

void win32_mutex::unlock() noexcept
{
    ::ReleaseMutex( static_cast< HANDLE >( handle_ ) );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_MUTEX
