// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>
#include <type_traits>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>
#include <nova/sync/mutex/policies.hpp>
#include <nova/sync/thread_safety/annotations.hpp>


namespace nova::sync {

//----------------------------------------------------------------------------------------------------------------------
// Concrete implementation classes (slow paths compiled into a .cpp TU)

namespace impl {

/// @brief parking_mutex without backoff, using std::atomic::wait (untimed).
class NOVA_SYNC_CAPABILITY( "mutex" ) parking_mutex_plain
{
public:
    parking_mutex_plain()                                        = default;
    ~parking_mutex_plain()                                       = default;
    parking_mutex_plain( const parking_mutex_plain& )            = delete;
    parking_mutex_plain& operator=( const parking_mutex_plain& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow( expected );
    }

    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t expected = 0;
        return state_.compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed );
    }

    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );
        if ( prev > 1 ) {
#ifdef __linux__
            atomic_notify_one( state_ );
#else
            state_.notify_one();
#endif
        }
    }

protected:
    // State layout:
    // Bit 0     : Lock status (0 = Free, 1 = Locked)
    // Bits 1-31 : Number of sleeping threads (Waiter count)
    std::atomic< uint32_t > state_ { 0 };

private:
    void lock_slow( uint32_t expected ) noexcept;
};

/// @brief parking_mutex with exponential backoff, using std::atomic::wait (untimed).
class NOVA_SYNC_CAPABILITY( "mutex" ) parking_mutex_backoff : protected parking_mutex_plain
{
public:
    parking_mutex_backoff()                                          = default;
    ~parking_mutex_backoff()                                         = default;
    parking_mutex_backoff( const parking_mutex_backoff& )            = delete;
    parking_mutex_backoff& operator=( const parking_mutex_backoff& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow( expected );
    }

    using parking_mutex_plain::try_lock;
    using parking_mutex_plain::unlock;

private:
    void lock_slow( uint32_t expected ) noexcept;
};

/// @brief parking_mutex without backoff, using futex-based atomic_wait (timed-capable).
class NOVA_SYNC_CAPABILITY( "mutex" ) parking_mutex_timed
{
public:
    parking_mutex_timed()                                        = default;
    ~parking_mutex_timed()                                       = default;
    parking_mutex_timed( const parking_mutex_timed& )            = delete;
    parking_mutex_timed& operator=( const parking_mutex_timed& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow( expected );
    }

    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t expected = 0;
        return state_.compare_exchange_strong( expected, 1, std::memory_order_acquire, std::memory_order_relaxed );
    }

    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        uint32_t prev = state_.fetch_and( ~1u, std::memory_order_release );
        if ( prev > 1 )
            atomic_notify_one( state_ );
    };

    template < class Rep, class Period >
    [[nodiscard]] bool try_lock_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    template < class Clock, class Duration >
    [[nodiscard]] bool try_lock_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( try_lock() )
            return true;
        return lock_slow_until( abs_time );
    }

protected:
    std::atomic< uint32_t > state_ { 0 };

private:
    void lock_slow( uint32_t expected ) noexcept;

    template < class Clock, class Duration >
    bool lock_slow_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        state_.fetch_add( 2, std::memory_order_relaxed );
        uint32_t expected = state_.load( std::memory_order_relaxed );

        while ( true ) {
            if ( ( expected & 1 ) == 0 ) {
                uint32_t desired = ( expected - 2 ) | 1;
                if ( state_.compare_exchange_weak(
                         expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                    return true;
                continue;
            }

            if ( !atomic_wait_until( state_, expected, abs_time, std::memory_order_relaxed ) ) {
                // Timed out — undo waiter registration
                expected = state_.load( std::memory_order_relaxed );
                while ( true ) {
                    if ( ( expected & 1 ) == 0 ) {
                        uint32_t desired = ( expected - 2 ) | 1;
                        if ( state_.compare_exchange_weak(
                                 expected, desired, std::memory_order_acquire, std::memory_order_relaxed ) )
                            return true;
                        continue;
                    }
                    if ( state_.compare_exchange_weak(
                             expected, expected - 2, std::memory_order_relaxed, std::memory_order_relaxed ) )
                        return false;
                }
            }

            expected = state_.load( std::memory_order_relaxed );
        }
    }
};

/// @brief parking_mutex with exponential backoff, using futex-based atomic_wait (timed-capable).
class NOVA_SYNC_CAPABILITY( "mutex" ) parking_mutex_timed_backoff : protected parking_mutex_timed
{
public:
    parking_mutex_timed_backoff()                                                = default;
    ~parking_mutex_timed_backoff()                                               = default;
    parking_mutex_timed_backoff( const parking_mutex_timed_backoff& )            = delete;
    parking_mutex_timed_backoff& operator=( const parking_mutex_timed_backoff& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow( expected );
    }

    using parking_mutex_timed::try_lock;
    using parking_mutex_timed::try_lock_for;
    using parking_mutex_timed::try_lock_until;
    using parking_mutex_timed::unlock;

private:
    void lock_slow( uint32_t expected ) noexcept;
};

} // namespace impl

//----------------------------------------------------------------------------------------------------------------------

/// @brief Futex-based parking mutex with optional exponential backoff and timed waits.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy         | Effect                                                       |
/// |----------------|--------------------------------------------------------------|
/// | `with_backoff` | Spin with exponential backoff before parking.                |
/// | `timed`        | Use futex-based waits (enables try_lock_for/try_lock_until). |
template < typename... Policies >
    requires( parameter::valid_parameters< std::tuple< detail::exponential_backoff_tag, detail::timed_tag >, Policies... > )
class NOVA_SYNC_CAPABILITY( "mutex" ) parking_mutex :
    public std::conditional_t<
        detail::has_timed_v< Policies... >,
        std::conditional_t< detail::has_backoff_v< Policies... >, impl::parking_mutex_timed_backoff, impl::parking_mutex_timed >,
        std::conditional_t< detail::has_backoff_v< Policies... >, impl::parking_mutex_backoff, impl::parking_mutex_plain > >
{};

} // namespace nova::sync
