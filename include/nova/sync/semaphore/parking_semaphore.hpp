// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>
#include <nova/sync/mutex/policies.hpp>

namespace nova::sync {

/// @brief Lock-free counting semaphore with optional exponential backoff.
///
/// Uses futex-based atomic wait/notify for blocking. With `with_backoff`, the
/// `acquire()` slow path spins before sleeping.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy        | Effect                                                |
/// |---------------|-------------------------------------------------------|
/// | (no exponential_backoff)  | Park immediately when count is negative (default).    |
/// | `with_backoff`| Spin with exponential backoff before parking.         |
///
/// ### Aliases
/// - `parking_semaphore<>`             — pure park, no spinning.
/// - `parking_semaphore<with_backoff>` — spin-then-park.
/// - `fast_semaphore`                  — deprecated alias for `parking_semaphore<>`.
template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
class parking_semaphore
{
    std::atomic< int32_t > count_;

    static constexpr bool use_backoff = detail::has_backoff_v< Policies... >;

public:
    explicit parking_semaphore( std::ptrdiff_t initial = 0 ) noexcept :
        count_( int32_t( initial ) )
    {
        assert( initial >= 0 );
    }

    ~parking_semaphore()                                     = default;
    parking_semaphore( const parking_semaphore& )            = delete;
    parking_semaphore& operator=( const parking_semaphore& ) = delete;

    /// @brief Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept
    {
        assert( n >= 0 );
        auto prev = count_.fetch_add( int32_t( n ), std::memory_order_release );
        if ( prev < 0 ) {
            auto to_wake = std::min( int32_t( n ), -prev );
            if ( to_wake == 1 )
                atomic_notify_one( count_ );
            else
                atomic_notify_all( count_ );
        }
    }

    /// @brief Blocks until a token is available, then consumes one.
    void acquire() noexcept
    {
        if constexpr ( use_backoff ) {
            detail::exponential_backoff backoff;
            while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
                auto c = count_.load( std::memory_order_relaxed );
                if ( c > 0 ) {
                    if ( count_.compare_exchange_weak( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                        return;
                }
                backoff.run();
            }
        }

        auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return;

        while ( true ) {
            auto c = count_.load( std::memory_order_relaxed );
            if ( c >= 0 )
                return;
            atomic_wait( count_, c, std::memory_order_acquire );
        }
    }

    /// @brief Consumes a token if available.
    /// @return `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept
    {
        auto c = count_.load( std::memory_order_relaxed );
        while ( c > 0 ) {
            if ( count_.compare_exchange_strong( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }
};

/// @brief Timed lock-free counting semaphore with optional exponential backoff.
///
/// Adds `try_acquire_for` / `try_acquire_until` to `parking_semaphore`.
///
/// | Policy        | Effect                                                |
/// |---------------|-------------------------------------------------------|
/// | (absence of `with_backoff`)  | Park immediately when count is negative (default).    |
/// | `with_backoff`| Spin with exponential backoff before parking.         |
///
/// ### Aliases
/// - `timed_semaphore<>`             — pure park, no spinning.
/// - `timed_semaphore<with_backoff>` — spin-then-park.
/// - `fast_timed_semaphore`                  — deprecated alias for `timed_semaphore<>`.
template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
class timed_semaphore
{
    std::atomic< int32_t > count_;

    static constexpr bool use_backoff = detail::has_backoff_v< Policies... >;

public:
    explicit timed_semaphore( std::ptrdiff_t initial = 0 ) noexcept :
        count_( int32_t( initial ) )
    {
        assert( initial >= 0 );
    }

    ~timed_semaphore()                                   = default;
    timed_semaphore( const timed_semaphore& )            = delete;
    timed_semaphore& operator=( const timed_semaphore& ) = delete;

    /// @brief Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept
    {
        assert( n >= 0 );
        auto prev = count_.fetch_add( int32_t( n ), std::memory_order_release );
        if ( prev < 0 ) {
            auto to_wake = std::min( int32_t( n ), -prev );
            if ( to_wake == 1 )
                atomic_notify_one( count_ );
            else
                atomic_notify_all( count_ );
        }
    }

    /// @brief Blocks until a token is available, then consumes one.
    void acquire() noexcept
    {
        if constexpr ( use_backoff ) {
            detail::exponential_backoff backoff;
            while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
                auto c = count_.load( std::memory_order_relaxed );
                if ( c > 0 ) {
                    if ( count_.compare_exchange_weak( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                        return;
                }
                backoff.run();
            }
        }

        auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return;

        while ( true ) {
            auto c = count_.load( std::memory_order_relaxed );
            if ( c >= 0 )
                return;
            atomic_wait( count_, c, std::memory_order_acquire );
        }
    }

    /// @brief Consumes a token if available.
    /// @return `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept
    {
        auto c = count_.load( std::memory_order_relaxed );
        while ( c > 0 ) {
            if ( count_.compare_exchange_strong( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Blocks until a token is available or the deadline is reached.
    /// @return `true` if acquired, `false` if timed out.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_acquire_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( try_acquire() )
            return true;

        auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return true;

        while ( true ) {
            auto c = count_.load( std::memory_order_relaxed );
            if ( c >= 0 )
                return true;

            if ( !atomic_wait_until( count_, c, abs_time, std::memory_order_acquire ) ) {
                auto restored = count_.fetch_add( 1, std::memory_order_relaxed );
                if ( restored >= 0 ) {
                    count_.fetch_sub( 1, std::memory_order_relaxed );
                    return true;
                }
                return false;
            }
        }
    }

    /// @brief Blocks until a token is available or the duration expires.
    /// @return `true` if acquired, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_acquire_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_acquire_until( std::chrono::steady_clock::now() + rel_time );
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Convenience aliases

/// @brief Deprecated alias for `parking_semaphore<>` (pure park, no backoff).
using fast_semaphore = parking_semaphore<>;

/// @brief Deprecated alias for `timed_semaphore<>` (pure park, no backoff).
using fast_timed_semaphore = timed_semaphore<>;

} // namespace nova::sync
