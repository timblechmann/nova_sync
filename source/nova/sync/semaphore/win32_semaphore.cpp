// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/semaphore/win32_semaphore.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_SEMAPHORE

#  include <windows.h>

#  include <cassert>
#  include <limits.h>

namespace nova::sync {

win32_semaphore::win32_semaphore( std::ptrdiff_t initial )
{
    handle_ = ::CreateSemaphoreW( nullptr, LONG( initial ), LONG_MAX, nullptr );
    assert( handle_ != nullptr && "CreateSemaphore failed" );
}

win32_semaphore::~win32_semaphore()
{
    if ( handle_ )
        ::CloseHandle( handle_ );
}

void win32_semaphore::release( std::ptrdiff_t n ) noexcept
{
    assert( n >= 0 && "win32_semaphore::release: n must be non-negative" );
    ::ReleaseSemaphore( handle_, LONG( n ), nullptr );
}

void win32_semaphore::acquire() noexcept
{
    ::WaitForSingleObject( handle_, INFINITE );
}

bool win32_semaphore::try_acquire() noexcept
{
    return ::WaitForSingleObject( handle_, 0 ) == WAIT_OBJECT_0;
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SEMAPHORE
