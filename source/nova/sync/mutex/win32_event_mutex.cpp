// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_event_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_EVENT_MUTEX

#  include <windows.h>

#  include <cassert>

namespace nova::sync {

win32_event_mutex::win32_event_mutex()
{
    // Use a semaphore with maximum count 1 to provide a non-recursive mutex
    // behavior.
    handle_ = ::CreateSemaphoreW( nullptr, 1, 1, nullptr );
    assert( handle_ != nullptr && "CreateSemaphore failed" );

    event_ = ::CreateEventW( nullptr, FALSE /*auto-reset*/, FALSE /*not signalled*/, nullptr );
    assert( event_ != nullptr && "CreateEvent failed" );
}

win32_event_mutex::~win32_event_mutex()
{
    if ( handle_ )
        ::CloseHandle( handle_ );
    if ( event_ )
        ::CloseHandle( event_ );
}

void win32_event_mutex::lock() noexcept
{
    ::WaitForSingleObject( handle_, INFINITE );
}

bool win32_event_mutex::try_lock() noexcept
{
    return ::WaitForSingleObject( handle_, 0 ) == WAIT_OBJECT_0;
}

void win32_event_mutex::unlock() noexcept
{
    ::ReleaseSemaphore( handle_, 1, nullptr );

    uint32_t s = state_.load( std::memory_order_relaxed );
    if ( ( s >> 1 ) != 0 && event_ )
        ::SetEvent( event_ );
}

void win32_event_mutex::consume_lock() const noexcept
{
    if ( event_ )
        ::WaitForSingleObject( event_, 0 );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_EVENT_MUTEX
