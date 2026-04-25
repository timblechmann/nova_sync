// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX

#  include <windows.h>

#  include <cassert>

namespace nova::sync {

win32_mutex::win32_mutex()
{
    // Use a semaphore with maximum count 1 to provide non-recursive mutex behavior.
    // CreateMutex is recursive by default, so we use CreateSemaphore instead.
    handle_ = ::CreateSemaphoreW( nullptr, 1, 1, nullptr );
    assert( handle_ != nullptr && "CreateSemaphore failed" );
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

void win32_mutex::unlock() noexcept
{
    ::ReleaseSemaphore( static_cast< HANDLE >( handle_ ), 1, nullptr );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_MUTEX
