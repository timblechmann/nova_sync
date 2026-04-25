// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/annotations.hpp>

#if defined( _WIN32 )
#    define NOVA_SYNC_HAS_WIN32_SRW_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX

typedef union SRWLOCK SRWLOCK;

namespace nova::sync {

/// @brief Ultra-lightweight non-recursive, non-fair mutex using Win32 SRW lock.
///
class NOVA_SYNC_CAPABILITY( "mutex" ) win32_srw_mutex
{
public:
    /// @brief Initialises the SRW lock.
    win32_srw_mutex() noexcept;

    ~win32_srw_mutex() = default;

    win32_srw_mutex( const win32_srw_mutex& )            = delete;
    win32_srw_mutex& operator=( const win32_srw_mutex& ) = delete;

    /// @brief Acquires the lock in exclusive mode.
    void lock() noexcept NOVA_SYNC_ACQUIRE();

    /// @brief Attempts to acquire in exclusive mode without blocking.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true );

    /// @brief Releases from exclusive mode.
    void unlock() noexcept NOVA_SYNC_RELEASE();

private:
    // SRWLOCK is pointer-sized (RTL_SRWLOCK internally); store as void*
    void* srwlock_ { nullptr };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SRW_MUTEX
