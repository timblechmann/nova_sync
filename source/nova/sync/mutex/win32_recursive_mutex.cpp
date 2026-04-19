// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_recursive_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_RECURSIVE_MUTEX

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <synchapi.h> // Minimal header for CRITICAL_SECTION only

#    include <cassert>
#    include <cstring>

namespace nova::sync {

static_assert( sizeof( CRITICAL_SECTION ) <= win32_recursive_mutex::storage_size,
               "win32_recursive_mutex::storage_size is too small for CRITICAL_SECTION" );
static_assert( alignof( CRITICAL_SECTION ) <= win32_recursive_mutex::storage_align,
               "win32_recursive_mutex::storage_align is too small for CRITICAL_SECTION" );

static CRITICAL_SECTION* cs_ptr( unsigned char* storage ) noexcept
{
    return std::launder( reinterpret_cast< CRITICAL_SECTION* >( storage ) );
}

win32_recursive_mutex::win32_recursive_mutex()
{
    static_cast< void >( std::memset( storage_, 0, sizeof( storage_ ) ) );
    ::InitializeCriticalSectionAndSpinCount( cs_ptr( storage_ ), 4000 );
}

win32_recursive_mutex::~win32_recursive_mutex()
{
    ::DeleteCriticalSection( cs_ptr( storage_ ) );
}

void win32_recursive_mutex::lock() noexcept
{
    ::EnterCriticalSection( cs_ptr( storage_ ) );
}

bool win32_recursive_mutex::try_lock() noexcept
{
    return ::TryEnterCriticalSection( cs_ptr( storage_ ) ) != FALSE;
}

void win32_recursive_mutex::unlock() noexcept
{
    ::LeaveCriticalSection( cs_ptr( storage_ ) );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_RECURSIVE_MUTEX
