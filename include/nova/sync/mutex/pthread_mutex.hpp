// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __has_include( <pthread.h> ) && __has_include( <unistd.h> )
#  include <pthread.h>
#  include <unistd.h>
#  if defined( _POSIX_THREADS ) && _POSIX_THREADS >= 0 && defined( _POSIX_TIMEOUTS ) && ( _POSIX_TIMEOUTS >= 0 )
#    define NOVA_SYNC_HAS_PTHREAD_MUTEX 1
#  endif
#  if defined( _POSIX_THREAD_PRIO_PROTECT ) && _POSIX_THREAD_PRIO_PROTECT >= 0
#    define NOVA_SYNC_HAS_PTHREAD_RT_MUTEX 1
#  endif
#endif

#ifdef NOVA_SYNC_HAS_PTHREAD_MUTEX

#  include <cassert>
#  include <chrono>
#  include <ctime>

#  include <nova/sync/detail/compat.hpp>
#  include <nova/sync/mutex/concepts.hpp>
#  include <nova/sync/mutex/policies.hpp>
#  include <nova/sync/thread_safety/macros.hpp>

namespace nova::sync {

/// @brief POSIX mutex with configurable policy.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy                  | Effect                                                              |
/// |-------------------------|---------------------------------------------------------------------|
/// | `priority_inherit`      | PTHREAD_PRIO_INHERIT — owner is boosted to highest waiter priority. |
/// | `priority_ceiling<N>`   | PTHREAD_PRIO_PROTECT — all holders are elevated to ceiling N.       |
/// | `pthread_recursive`     | PTHREAD_MUTEX_RECURSIVE — allows re-entrant locking.                |
/// | `pthread_errorcheck`    | PTHREAD_MUTEX_ERRORCHECK — returns error on double-lock.            |
/// | `pthread_adaptive`      | PTHREAD_MUTEX_ADAPTIVE_NP — adaptive spin before blocking (Linux).  |
///
/// At most one of `priority_inherit` / `priority_ceiling<N>` may be given (mutually exclusive).
/// At most one of `pthread_recursive` / `pthread_errorcheck` / `pthread_adaptive` may be given.
///
/// ### Availability
///
/// This header is available when the system provides POSIX threads (`_POSIX_THREADS`).
/// Priority protocol features (`priority_inherit`, `priority_ceiling`) additionally require
/// `_POSIX_THREAD_PRIO_PROTECT`.
// pthread-specific policy definitions (kept here because they're pthread-only)
namespace pthread_policy {
namespace tags {
struct priority_inherit_tag
{};
struct priority_ceiling_tag
{};
struct pthread_errorcheck_tag
{};
struct pthread_adaptive_tag
{};
} // namespace tags

using priority_inherit = parameter::flag_param< tags::priority_inherit_tag >;
template < int Ceiling >
using priority_ceiling   = parameter::integral_param< tags::priority_ceiling_tag, int, Ceiling >;
using pthread_errorcheck = parameter::flag_param< tags::pthread_errorcheck_tag >;
using pthread_adaptive   = parameter::flag_param< tags::pthread_adaptive_tag >;

using pthread_allowed_tags = std::tuple< tags::priority_inherit_tag,
                                         tags::priority_ceiling_tag,
                                         detail::recursive_tag,
                                         tags::pthread_errorcheck_tag,
                                         tags::pthread_adaptive_tag >;

template < typename... Policies >
inline constexpr bool has_priority_inherit_v = parameter::has_parameter_v< tags::priority_inherit_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_priority_ceiling_v = parameter::has_parameter_v< tags::priority_ceiling_tag, Policies... >;

template < typename... Policies >
inline constexpr int extract_priority_ceiling_v
    = parameter::extract_integral_v< tags::priority_ceiling_tag, int, 0, Policies... >;

template < typename... Policies >
inline constexpr bool has_pthread_errorcheck_v = parameter::has_parameter_v< tags::pthread_errorcheck_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_pthread_adaptive_v = parameter::has_parameter_v< tags::pthread_adaptive_tag, Policies... >;
} // namespace pthread_policy

template < typename... Policies >
    requires( parameter::valid_parameters< pthread_policy::pthread_allowed_tags, Policies... >
              && !( pthread_policy::has_priority_inherit_v< Policies... >
                    && pthread_policy::has_priority_ceiling_v< Policies... > )
              && !( detail::has_recursive_v< Policies... > && pthread_policy::has_pthread_errorcheck_v< Policies... > )
              && !( detail::has_recursive_v< Policies... > && pthread_policy::has_pthread_adaptive_v< Policies... > )
              && !(pthread_policy::has_pthread_errorcheck_v< Policies... >
                   && pthread_policy::has_pthread_adaptive_v< Policies... >))
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY pthread_mutex
{
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;

    static constexpr bool use_inherit = pthread_policy::has_priority_inherit_v< Policies... >;
    static constexpr bool use_ceiling = pthread_policy::has_priority_ceiling_v< Policies... >;
    static constexpr int  ceiling_val = pthread_policy::extract_priority_ceiling_v< Policies... >;
    static constexpr bool use_rt      = use_inherit || use_ceiling;
    static constexpr int  protocol    = use_inherit ? PTHREAD_PRIO_INHERIT : PTHREAD_PRIO_PROTECT;

    static constexpr bool use_recursive  = detail::has_recursive_v< Policies... >;
    static constexpr bool use_errorcheck = pthread_policy::has_pthread_errorcheck_v< Policies... >;
    static constexpr bool use_adaptive   = pthread_policy::has_pthread_adaptive_v< Policies... >;

public:
    /// @brief Constructs the mutex (policy determined by template parameters).
    explicit pthread_mutex()
    {
        initialize();
    }

    /// @brief Destroys the mutex.
    ~pthread_mutex()
    {
        pthread_mutex_destroy( &mutex_ );
    }

    pthread_mutex( const pthread_mutex& )            = delete;
    pthread_mutex& operator=( const pthread_mutex& ) = delete;

    /// @brief Acquires the lock, blocking until available.
    void lock() NOVA_SYNC_ACQUIRE()
    {
        pthread_mutex_lock( &mutex_ );
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return pthread_mutex_trylock( &mutex_ ) == 0;
    }

    /// @brief Releases the lock.
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        pthread_mutex_unlock( &mutex_ );
    }

    /// @brief Attempts to acquire the lock for up to the given duration.
    /// @return `true` if acquired, `false` if timeout or error.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    /// @brief Attempts to acquire the lock until the given time point.
    /// @return `true` if acquired, `false` if timeout or error.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock > ) {
            auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
            auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
            auto nsecs = ns - secs;

            struct timespec ts {
                .tv_sec  = time_t( secs.count() ),
                .tv_nsec = long( nsecs.count() ),
            };

#  if defined( __linux__ )
            return pthread_mutex_clocklock( &mutex_, CLOCK_REALTIME, &ts ) == 0;
#  else
            return pthread_mutex_timedlock( &mutex_, &ts ) == 0;
#  endif
#  if defined( __linux__ )
        } else if constexpr ( std::is_same_v< Clock, std::chrono::steady_clock > ) {
            auto ns    = std::chrono::time_point_cast< std::chrono::nanoseconds >( abs_time ).time_since_epoch();
            auto secs  = std::chrono::duration_cast< std::chrono::seconds >( ns );
            auto nsecs = ns - secs;

            struct timespec ts {
                .tv_sec  = time_t( secs.count() ),
                .tv_nsec = long( nsecs.count() ),
            };

            return pthread_mutex_clocklock( &mutex_, CLOCK_MONOTONIC, &ts ) == 0;
#  endif
        } else {
            auto rel_time = abs_time - Clock::now();
            return try_lock_for( rel_time );
        }
    }

private:
    void initialize()
    {
        pthread_mutexattr_t attr;
        int                 result = pthread_mutexattr_init( &attr );
        assert( result == 0 && "pthread_mutexattr_init failed" );

        if constexpr ( use_recursive ) {
            result = pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
            assert( result == 0 && "pthread_mutexattr_settype(RECURSIVE) failed" );
        } else if constexpr ( use_errorcheck ) {
            result = pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
            assert( result == 0 && "pthread_mutexattr_settype(ERRORCHECK) failed" );
        }
#  ifdef __linux__
        else if constexpr ( use_adaptive ) {
            result = pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ADAPTIVE_NP );
            assert( result == 0 && "pthread_mutexattr_settype(ADAPTIVE_NP) failed" );
        }
#  endif

#  ifdef NOVA_SYNC_HAS_PTHREAD_RT_MUTEX
        if constexpr ( use_rt ) {
            result = pthread_mutexattr_setprotocol( &attr, protocol );
            assert( result == 0 && "pthread_mutexattr_setprotocol failed" );

            if constexpr ( use_ceiling ) {
                result = pthread_mutexattr_setprioceiling( &attr, ceiling_val );
                assert( result == 0 && "pthread_mutexattr_setprioceiling failed" );
            }
        }
#  endif

        result = pthread_mutex_init( &mutex_, &attr );
        assert( result == 0 && "pthread_mutex_init failed" );

        pthread_mutexattr_destroy( &attr );
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Convenience aliases

/// @brief Default pthread mutex.
using pthread_default_mutex = pthread_mutex<>;

/// @brief Priority-inherit pthread mutex.
using pthread_priority_inherit_mutex = pthread_mutex< pthread_policy::priority_inherit >;

/// @brief Priority-ceiling pthread mutex with compile-time ceiling value.
/// @tparam Ceiling  The priority ceiling level.
template < int Ceiling >
using pthread_priority_ceiling_mutex = pthread_mutex< pthread_policy::priority_ceiling< Ceiling > >;

/// @brief Recursive pthread mutex.
using pthread_recursive_mutex = pthread_mutex< recursive >;

//----------------------------------------------------------------------------------------------------------------------
// Concept specializations

namespace concepts {

template < typename... Policies >
    requires( detail::has_recursive_v< Policies... > )
struct concepts_is_recursive< nova::sync::pthread_mutex< Policies... > > : std::true_type
{};

} // namespace concepts

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_PTHREAD_MUTEX
