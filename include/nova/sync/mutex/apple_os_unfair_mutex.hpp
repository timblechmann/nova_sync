// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( __APPLE__ )
#  define NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX 1
#  include <os/lock.h>
#endif

#ifdef NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX

#  include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

/// @brief High-performance mutex using Apple's `os_unfair_lock`.
///
class OS_UNFAIR_LOCK_AVAILABILITY NOVA_SYNC_CAPABILITY( "mutex" ) apple_os_unfair_mutex
{
public:
    /// @brief Initialises the lock in the unlocked state.
    apple_os_unfair_mutex() noexcept = default;
    ~apple_os_unfair_mutex()         = default;

    apple_os_unfair_mutex( const apple_os_unfair_mutex& )            = delete;
    apple_os_unfair_mutex& operator=( const apple_os_unfair_mutex& ) = delete;

    /// @brief Acquires the lock, blocking until available.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        os_unfair_lock_lock( &lock_ );
    }

    /// @brief Attempts to acquire the lock without blocking.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return os_unfair_lock_trylock( &lock_ );
    }

    /// @brief Releases the lock.
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        os_unfair_lock_unlock( &lock_ );
    }

private:
    os_unfair_lock lock_ { OS_UNFAIR_LOCK_INIT };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX
