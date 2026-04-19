// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#    define NOVA_SYNC_HAS_WIN32_RECURSIVE_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_RECURSIVE_MUTEX

#    include <nova/sync/detail/compat.hpp>
#    include <nova/sync/mutex/concepts.hpp>

namespace nova::sync {

/// @brief Recursive mutex implemented using Win32 CRITICAL_SECTION.
///
class alignas( detail::hardware_destructive_interference_size ) win32_recursive_mutex
{
public:
    /// @brief Constructs and initialises the CRITICAL_SECTION with a spin count.
    win32_recursive_mutex();

    /// @brief Destroys the CRITICAL_SECTION.
    ~win32_recursive_mutex();

    win32_recursive_mutex( const win32_recursive_mutex& )            = delete;
    win32_recursive_mutex& operator=( const win32_recursive_mutex& ) = delete;

    /// @brief Acquires the lock, spinning then blocking as necessary.
    ///        Re-entrant: safe to call from the already-owning thread.
    void lock() noexcept;

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if the lock was acquired (including recursive re-entry),
    ///         `false` if another thread currently holds it.
    bool try_lock() noexcept;

    /// @brief Releases one level of recursion; fully unlocks when count reaches zero.
    void unlock() noexcept;

private:
    // Opaque storage sized and aligned to hold a CRITICAL_SECTION without
    // pulling in <windows.h>.  The layout is validated in the .cpp file via a
    // static_assert against sizeof(CRITICAL_SECTION).
    //
    // CRITICAL_SECTION on 64-bit Windows is 40 bytes; keep a comfortable
    // margin so the assert in the .cpp catches any discrepancy at compile time.
    static constexpr unsigned storage_size  = 48;
    static constexpr unsigned storage_align = 8;

    alignas( storage_align ) unsigned char storage_[ storage_size ];
};

namespace concepts {
template <>
struct concepts_is_recursive< nova::sync::win32_recursive_mutex > : std::true_type
{};
} // namespace concepts

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_RECURSIVE_MUTEX
