// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __has_include( <pthread.h> )
#    include <pthread.h>
#    if _POSIX_C_SOURCE >= 200112L
#        define NOVA_SYNC_HAS_PTHREAD_SPINLOCK 1
#    endif
#endif

#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

#ifdef NOVA_SYNC_HAS_PTHREAD_SPINLOCK
class pthread_spinlock_mutex
{
    pthread_spinlock_t lock_;

public:
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

    void lock() noexcept
    {
        pthread_spin_lock( &lock_ );
    }

    bool try_lock() noexcept
    {
        return pthread_spin_trylock( &lock_ ) == 0;
    }

    void unlock() noexcept
    {
        pthread_spin_unlock( &lock_ );
    }
};
#endif

} // namespace nova::sync
