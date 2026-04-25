// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( _WIN32 )
#    define NOVA_SYNC_HAS_WIN32_SRW_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX

#    include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Ultra-lightweight non-recursive, non-fair mutex using Win32 SRW lock.
///
class alignas( detail::hardware_destructive_interference_size ) win32_srw_mutex
{
public:
    /// @brief Initialises the SRW lock.
    win32_srw_mutex() noexcept;
    ~win32_srw_mutex() = default;

    win32_srw_mutex( const win32_srw_mutex& )            = delete;
    win32_srw_mutex& operator=( const win32_srw_mutex& ) = delete;

    /// @brief Acquires the lock in exclusive mode.
    void lock() noexcept;

    /// @brief Attempts to acquire in exclusive mode without blocking.
    bool try_lock() noexcept;

    /// @brief Releases from exclusive mode.
    void unlock() noexcept;

private:
    struct lock
    {
        void* ptr {};
    } srwlock_;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SRW_MUTEX
