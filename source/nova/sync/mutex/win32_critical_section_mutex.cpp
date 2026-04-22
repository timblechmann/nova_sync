// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/win32_critical_section_mutex.hpp>

#ifdef NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef _WIN32_WINNT
#        define _WIN32_WINNT 0x0a00
#    endif

#    include <windows.h>

#    include <cassert>

namespace nova::sync {

win32_critical_section_mutex::win32_critical_section_mutex()
{
    // Validate storage size at compile-time
    static_assert( sizeof( CRITICAL_SECTION ) <= storage_size, "CRITICAL_SECTION is larger than allocated storage" );

    CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );

    // Initialize with a spin count for better performance on multi-core systems.
    // The spin count of 4000 is a heuristic that works well on modern hardware.
    BOOL result = ::InitializeCriticalSectionAndSpinCount( cs, 4000 );
    assert( result && "InitializeCriticalSectionAndSpinCount failed" );
}

win32_critical_section_mutex::~win32_critical_section_mutex()
{
    CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
    ::DeleteCriticalSection( cs );
}

void win32_critical_section_mutex::lock() noexcept
{
    CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
    ::EnterCriticalSection( cs );
}

bool win32_critical_section_mutex::try_lock() noexcept
{
    CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
    return ::TryEnterCriticalSection( cs ) != 0;
}

void win32_critical_section_mutex::unlock() noexcept
{
    CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
    ::LeaveCriticalSection( cs );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX
