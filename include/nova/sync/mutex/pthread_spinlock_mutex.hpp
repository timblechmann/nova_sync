// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __has_include( <pthread.h> )
#  include <pthread.h>
#  if _POSIX_C_SOURCE >= 200112L
#    define NOVA_SYNC_HAS_PTHREAD_SPINLOCK 1
#  endif
#endif

#include <nova/sync/mutex/tsa_annotations.hpp>

namespace nova::sync {

#ifdef NOVA_SYNC_HAS_PTHREAD_SPINLOCK
/// @brief Spinlock mutex wrapping POSIX `pthread_spinlock_t`.
class NOVA_SYNC_CAPABILITY( "mutex" ) pthread_spinlock_mutex
{
    pthread_spinlock_t lock_;

public:
    /// @brief Constructs an unlocked pthread spinlock mutex.
    pthread_spinlock_mutex()
    {
        pthread_spin_init( &lock_, PTHREAD_PROCESS_PRIVATE );
    }

    ~pthread_spinlock_mutex()
    {
        pthread_spin_destroy( &lock_ );
    }

    pthread_spinlock_mutex( const pthread_spinlock_mutex& )            = delete;
    pthread_spinlock_mutex& operator=( const pthread_spinlock_mutex& ) = delete;

    /// @brief Acquires the lock, spinning until available.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        pthread_spin_lock( &lock_ );
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return pthread_spin_trylock( &lock_ ) == 0;
    }

    /// @brief Releases the lock.
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        pthread_spin_unlock( &lock_ );
    }
};
#endif

} // namespace nova::sync
