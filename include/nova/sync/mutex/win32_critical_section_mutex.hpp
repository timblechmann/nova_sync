// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#  define NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX

#  include <cassert>
#  include <windows.h>

#  include <nova/sync/mutex/concepts.hpp>
#  include <nova/sync/mutex/policies.hpp>
#  include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

/// @brief Recursive mutex implemented using Win32 CRITICAL_SECTION.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy               | Effect                                                    |
/// |----------------------|-----------------------------------------------------------|
/// | `win32_spin_count<N>`| Spin count for InitializeCriticalSectionAndSpinCount.    |
///
// Win32-specific policy: spin count for InitializeCriticalSectionAndSpinCount
namespace win32_policy {
namespace tags {
struct win32_spin_count_tag
{};
} // namespace tags

template < unsigned Count >
using win32_spin_count = parameter::integral_param< tags::win32_spin_count_tag, unsigned, Count >;

using win32_cs_allowed_tags = std::tuple< tags::win32_spin_count_tag >;

template < typename... Policies >
inline constexpr unsigned extract_win32_spin_count_v
    = parameter::extract_integral_v< tags::win32_spin_count_tag, unsigned, 4000u, Policies... >;
} // namespace win32_policy

template < typename... Policies >
    requires( parameter::valid_parameters< win32_policy::win32_cs_allowed_tags, Policies... > )
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY win32_critical_section_mutex
{
    static constexpr unsigned storage_size  = 48;
    static constexpr unsigned storage_align = 8;
    static constexpr unsigned spin_count    = win32_policy::extract_win32_spin_count_v< Policies... >;

    alignas( storage_align ) unsigned char storage_[ storage_size ] {};

public:
    /// @brief Constructs an unlocked mutex.
    win32_critical_section_mutex()
    {
        static_assert( sizeof( CRITICAL_SECTION ) <= storage_size, "CRITICAL_SECTION is larger than allocated storage" );
        CRITICAL_SECTION* cs     = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
        BOOL              result = ::InitializeCriticalSectionAndSpinCount( cs, spin_count );
        assert( result && "InitializeCriticalSectionAndSpinCount failed" );
    }

    ~win32_critical_section_mutex()
    {
        CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
        ::DeleteCriticalSection( cs );
    }

    win32_critical_section_mutex( const win32_critical_section_mutex& )            = delete;
    win32_critical_section_mutex& operator=( const win32_critical_section_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary. Re-entrant.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
        ::EnterCriticalSection( cs );
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked by another thread.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
        return ::TryEnterCriticalSection( cs ) != 0;
    }

    /// @brief Releases one level of recursion.
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        CRITICAL_SECTION* cs = reinterpret_cast< CRITICAL_SECTION* >( storage_ );
        ::LeaveCriticalSection( cs );
    }
};

namespace concepts {
template < typename... Policies >
struct concepts_is_recursive< nova::sync::win32_critical_section_mutex< Policies... > > : std::true_type
{};
} // namespace concepts

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX
