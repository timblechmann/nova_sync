// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#  define NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX

#  include <nova/sync/mutex/concepts.hpp>
#  include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

/// @brief Recursive mutex implemented using Win32 CRITICAL_SECTION.
///
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY win32_critical_section_mutex
{
public:
    /// @brief Constructs an unlocked mutex.
    win32_critical_section_mutex();

    ~win32_critical_section_mutex();

    win32_critical_section_mutex( const win32_critical_section_mutex& )            = delete;
    win32_critical_section_mutex& operator=( const win32_critical_section_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary. Re-entrant.
    void lock() noexcept NOVA_SYNC_ACQUIRE();

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked by another thread.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true );

    /// @brief Releases one level of recursion.
    void unlock() noexcept NOVA_SYNC_RELEASE();

private:
    static constexpr unsigned storage_size  = 48;
    static constexpr unsigned storage_align = 8;

    alignas( storage_align ) unsigned char storage_[ storage_size ] {};
};

namespace concepts {
template <>
struct concepts_is_recursive< nova::sync::win32_critical_section_mutex > : std::true_type
{};
} // namespace concepts

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_CRITICAL_SECTION_MUTEX
