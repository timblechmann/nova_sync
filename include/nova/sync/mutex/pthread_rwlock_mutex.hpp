// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __has_include( <pthread.h> ) && __has_include( <unistd.h> )
#  include <pthread.h>
#  include <unistd.h>
#  if defined( _POSIX_THREADS ) && _POSIX_THREADS >= 0 && defined( _POSIX_READER_WRITER_LOCKS ) \
      && _POSIX_READER_WRITER_LOCKS >= 0 && defined( _POSIX_TIMEOUTS ) && _POSIX_TIMEOUTS >= 0
#    define NOVA_SYNC_HAS_PTHREAD_RWLOCK 1
#  endif
#endif

#ifdef NOVA_SYNC_HAS_PTHREAD_RWLOCK

#  include <cassert>
#  include <chrono>
#  include <time.h>

#  include <nova/sync/detail/compat.hpp>
#  include <nova/sync/mutex/concepts.hpp>
#  include <nova/sync/thread_safety/macros.hpp>

namespace nova::sync {

/// @brief POSIX read-write mutex wrapper around pthread_rwlock_t.
///
/// Availability: only when the platform provides POSIX reader-writer locks.
class NOVA_SYNC_CAPABILITY( "shared_mutex" ) pthread_rwlock_mutex
{
    pthread_rwlock_t rw_ = PTHREAD_RWLOCK_INITIALIZER;

public:
    pthread_rwlock_mutex()
    {
        [[maybe_unused]] int r = pthread_rwlock_init( &rw_, nullptr );
        assert( r == 0 && "pthread_rwlock_init failed" );
    }

    ~pthread_rwlock_mutex()
    {
        pthread_rwlock_destroy( &rw_ );
    }

    pthread_rwlock_mutex( const pthread_rwlock_mutex& )            = delete;
    pthread_rwlock_mutex& operator=( const pthread_rwlock_mutex& ) = delete;

    // Exclusive (write) lock
    void lock() NOVA_SYNC_ACQUIRE()
    {
        pthread_rwlock_wrlock( &rw_ );
    }

    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return pthread_rwlock_trywrlock( &rw_ ) == 0;
    }

    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        pthread_rwlock_unlock( &rw_ );
    }

    // Shared (read) lock
    void lock_shared() NOVA_SYNC_ACQUIRE_SHARED()
    {
        pthread_rwlock_rdlock( &rw_ );
    }

    [[nodiscard]] bool try_lock_shared() noexcept NOVA_SYNC_TRY_ACQUIRE_SHARED( true )
    {
        return pthread_rwlock_tryrdlock( &rw_ ) == 0;
    }

    // Timed shared tries
    template < class Rep, class Period >
    bool try_lock_shared_for( const std::chrono::duration< Rep, Period >& rel_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE_SHARED( true )
    {
        return try_lock_shared_until( std::chrono::steady_clock::now() + rel_time );
    }

    template < class Clock, class Duration >
    bool try_lock_shared_until( const std::chrono::time_point< Clock, Duration >& abs_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE_SHARED( true )
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock > ) {
            auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
            auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
            auto nsecs = ns - secs;
            struct timespec ts {
                .tv_sec  = time_t( secs.count() ),
                .tv_nsec = long( nsecs.count() ),
            };
#  if defined( NOVA_SYNC_USE_PTHREAD_RWLOCK_CLOCK )
            return pthread_rwlock_clockrdlock( &rw_, CLOCK_REALTIME, &ts ) == 0;
#  else
            return pthread_rwlock_timedrdlock( &rw_, &ts ) == 0;
#  endif
        } else if constexpr ( std::is_same_v< Clock, std::chrono::steady_clock > ) {
#  ifdef __linux__
            // steady_clock -> CLOCK_MONOTONIC
            auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
            auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
            auto nsecs = ns - secs;
            struct timespec ts {
                .tv_sec  = time_t( secs.count() ),
                .tv_nsec = long( nsecs.count() ),
            };
            return pthread_rwlock_clockrdlock( &rw_, CLOCK_MONOTONIC, &ts ) == 0;
#  endif
        } else {
            auto rel_time = abs_time - Clock::now();
            return try_lock_shared_for( rel_time );
        }
    }

    void unlock_shared() noexcept NOVA_SYNC_RELEASE_SHARED()
    {
        pthread_rwlock_unlock( &rw_ );
    }

    // Timed exclusive tries: rely on POSIX timeouts being available
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time ) noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock > ) {
            auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
            auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
            auto nsecs = ns - secs;
            struct timespec ts {
                .tv_sec  = time_t( secs.count() ),
                .tv_nsec = long( nsecs.count() ),
            };
#  ifdef __linux__
            return pthread_rwlock_clockwrlock( &rw_, CLOCK_REALTIME, &ts ) == 0;
#  else
            return pthread_rwlock_timedrwlock( &rw_, &ts ) == 0;
#  endif
        } else if constexpr ( std::is_same_v< Clock, std::chrono::steady_clock > ) {
#  ifdef __linux__
            // steady_clock -> CLOCK_MONOTONIC
            auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
            auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
            auto nsecs = ns - secs;
            struct timespec ts {
                .tv_sec  = time_t( secs.count() ),
                .tv_nsec = long( nsecs.count() ),
            };
            return pthread_rwlock_clockwrlock( &rw_, CLOCK_MONOTONIC, &ts ) == 0;
#  endif
        } else {
            auto rel_time = abs_time - Clock::now();
            return try_lock_for( rel_time );
        }
    }
};

// convenience alias
using pthread_shared_mutex = pthread_rwlock_mutex;

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_PTHREAD_RWLOCK
