// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( _WIN32 )
#  define NOVA_SYNC_HAS_WIN32_SRW_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX

#  include <nova/sync/detail/compat.hpp>
#  include <nova/sync/mutex/annotations.hpp>

namespace nova::sync {

/// @brief Ultra-lightweight mutex using Win32 SRW lock.
///
class alignas( detail::hardware_destructive_interference_size ) NOVA_SYNC_CAPABILITY( "mutex" ) win32_srw_mutex
{
public:
    /// @brief Initialises the SRW lock in the unlocked state.
    win32_srw_mutex() noexcept;
    ~win32_srw_mutex() = default;

    win32_srw_mutex( const win32_srw_mutex& )            = delete;
    win32_srw_mutex& operator=( const win32_srw_mutex& ) = delete;

    /// @brief Acquires the lock, blocking until available.
    void lock() noexcept NOVA_SYNC_ACQUIRE();

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true );

    /// @brief Releases the lock.
    void unlock() noexcept NOVA_SYNC_RELEASE();

private:
    struct lock
    {
        void* ptr {};
    } srwlock_;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SRW_MUTEX
