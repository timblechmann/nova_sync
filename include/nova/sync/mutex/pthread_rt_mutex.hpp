// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __has_include( <pthread.h> ) && __has_include( <unistd.h> )
#    include <pthread.h>
#    include <unistd.h>
#    if defined( _POSIX_THREAD_PRIO_PROTECT ) && _POSIX_THREAD_PRIO_PROTECT >= 0
#        define NOVA_SYNC_HAS_PTHREAD_RT_MUTEX 1
#    endif
#endif

#ifdef NOVA_SYNC_HAS_PTHREAD_RT_MUTEX

#    include <chrono>
#    include <ctime>
#    include <stdexcept>

#    include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Priority protocol for pthread mutexes.
enum class pthread_mutex_policy : uint8_t
{
    /// @brief Priority ceiling (PTHREAD_PRIO_PROTECT).
    /// Any thread acquiring the lock is temporarily elevated to the ceiling priority.
    priority_ceiling = PTHREAD_PRIO_PROTECT,

    /// @brief Priority inheritance (PTHREAD_PRIO_INHERIT).
    /// The lock owner is boosted to any waiting higher-priority thread's level.
    priority_inherit = PTHREAD_PRIO_INHERIT,
};

/// @brief Priority ceiling value for pthread_priority_ceiling_mutex.
struct priority_ceiling
{
    int value; ///< The priority ceiling level.

    /// @brief Constructs a priority ceiling value.
    explicit constexpr priority_ceiling( int v ) noexcept :
        value( v )
    {}
};

/// @brief A POSIX mutex that implements priority protection or inheritance.
/// @details
/// Implements either PTHREAD_PRIO_PROTECT or PTHREAD_PRIO_INHERIT protocol,
/// selected via the \p Policy template parameter. Useful for real-time
/// applications to avoid priority inversion.
///
/// - **priority_ceiling**: The mutex carries a fixed priority ceiling. Any thread
///   acquiring the lock is temporarily elevated to that ceiling, protecting against
///   lower-priority preemption.
/// - **priority_inherit**: When a higher-priority thread waits on the mutex, the
///   current owner is temporarily boosted to the waiter's priority, ensuring
///   timely lock release.
///
/// Thread scheduling policy must be SCHED_FIFO or SCHED_RR for effective priority
/// protection; SCHED_OTHER threads may use PTHREAD_PRIO_INHERIT but PTHREAD_PRIO_PROTECT
/// will fail.
///
/// @tparam Policy  One of `pthread_mutex_policy::priority_ceiling` or
///                 `pthread_mutex_policy::priority_inherit`.
template < pthread_mutex_policy Policy >
class alignas( detail::hardware_destructive_interference_size ) pthread_rt_mutex
{
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;

public:
    /// @brief Constructs a priority-protected mutex with the given ceiling.
    /// @param ceiling  Priority ceiling value.
    /// @throws std::runtime_error if underlying POSIX calls fail.
    explicit pthread_rt_mutex( priority_ceiling ceiling )
        requires( Policy == pthread_mutex_policy::priority_ceiling )
    {
        initialize( ceiling );
    }

    /// @brief Constructs a priority-inherited mutex.
    /// @throws std::runtime_error if underlying POSIX calls fail.
    explicit pthread_rt_mutex()
        requires( Policy == pthread_mutex_policy::priority_inherit )
    {
        initialize();
    }

    /// @brief Destroys the mutex.
    ~pthread_rt_mutex()
    {
        pthread_mutex_destroy( &mutex_ );
    }

    pthread_rt_mutex( const pthread_rt_mutex& )            = delete;
    pthread_rt_mutex& operator=( const pthread_rt_mutex& ) = delete;

    /// @brief Acquires the lock, blocking until available.
    void lock()
    {
        pthread_mutex_lock( &mutex_ );
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if acquired, `false` if already locked.
    bool try_lock() noexcept
    {
        return pthread_mutex_trylock( &mutex_ ) == 0;
    }

    /// @brief Releases the lock.
    void unlock() noexcept
    {
        pthread_mutex_unlock( &mutex_ );
    }

    /// @brief Attempts to acquire the lock for up to the given duration.
    /// @param rel_time  Maximum duration to wait.
    /// @return `true` if acquired, `false` if timeout or error.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    /// @brief Attempts to acquire the lock until the given time point.
    /// @param abs_time  Absolute deadline.
    /// @return `true` if acquired, `false` if timeout or error.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time )
    {
        std::chrono::steady_clock::time_point steady_tp = std::chrono::time_point_cast< std::chrono::nanoseconds >(
            std::chrono::steady_clock::now()
            + std::chrono::duration_cast< std::chrono::nanoseconds >( abs_time - Clock::now() ) );
        return try_lock_until( steady_tp );
    }

    /// @brief Attempts to acquire the lock until the given steady_clock time point.
    /// @param abs_time  Absolute deadline (steady_clock).
    /// @return `true` if acquired, `false` if timeout or error.
    bool try_lock_until( const std::chrono::steady_clock::time_point& abs_time )
    {
        auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
        auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
        auto nsecs = ns - secs;

        struct timespec ts {
            .tv_sec  = time_t( secs.count() ),
            .tv_nsec = long( nsecs.count() ),
        };

        int ret = pthread_mutex_timedlock( &mutex_, &ts );
        return ret == 0;
    }

private:
    void initialize( std::optional< priority_ceiling > ceiling = std::nullopt )
    {
        pthread_mutexattr_t attr;
        if ( pthread_mutexattr_init( &attr ) != 0 )
            throw std::runtime_error( "pthread_mutexattr_init failed" );

        if ( pthread_mutexattr_setprotocol( &attr, static_cast< int >( Policy ) ) != 0 ) {
            pthread_mutexattr_destroy( &attr );
            throw std::runtime_error( "pthread_mutexattr_setprotocol failed" );
        }

        if constexpr ( Policy == pthread_mutex_policy::priority_ceiling ) {
            if ( pthread_mutexattr_setprioceiling( &attr, ceiling->value ) != 0 ) {
                pthread_mutexattr_destroy( &attr );
                throw std::runtime_error( "pthread_mutexattr_setprioceiling failed" );
            }
        }

        if ( pthread_mutex_init( &mutex_, &attr ) != 0 ) {
            pthread_mutexattr_destroy( &attr );
            throw std::runtime_error( "pthread_mutex_init failed" );
        }

        pthread_mutexattr_destroy( &attr );
    }
};

/// Convenience aliases
using pthread_priority_ceiling_mutex = pthread_rt_mutex< pthread_mutex_policy::priority_ceiling >;
using pthread_priority_inherit_mutex = pthread_rt_mutex< pthread_mutex_policy::priority_inherit >;

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_PTHREAD_RT_MUTEX
