// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>

namespace nova::sync {

/// Lock-free counting semaphore using atomic wait/notify.
class fast_semaphore
{
public:
    explicit fast_semaphore( std::ptrdiff_t initial = 0 ) noexcept :
        count_( int32_t( initial ) )
    {
        assert( initial >= 0 );
    }

    ~fast_semaphore()                                  = default;
    fast_semaphore( const fast_semaphore& )            = delete;
    fast_semaphore& operator=( const fast_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept
    {
        assert( n >= 0 );
        auto prev = count_.fetch_add( int32_t( n ), std::memory_order_release );
        if ( prev < 0 ) {
            auto to_wake = std::min( int32_t( n ), -prev );
            if ( to_wake == 1 )
                count_.notify_one();
            else
                count_.notify_all();
        }
    }

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept
    {
        auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return;

        while ( true ) {
            auto c = count_.load( std::memory_order_relaxed );
            if ( c >= 0 )
                return;
            count_.wait( c, std::memory_order_acquire );
        }
    }

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept
    {
        auto c = count_.load( std::memory_order_relaxed );
        while ( c > 0 ) {
            if ( count_.compare_exchange_strong( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

private:
    std::atomic< int32_t > count_;
};


/// Lock-free counting semaphore using atomic wait/notify. Supports timed waits
class fast_timed_semaphore
{
public:
    explicit fast_timed_semaphore( std::ptrdiff_t initial = 0 ) noexcept :
        count_( int32_t( initial ) )
    {
        assert( initial >= 0 );
    }

    ~fast_timed_semaphore()                                        = default;
    fast_timed_semaphore( const fast_timed_semaphore& )            = delete;
    fast_timed_semaphore& operator=( const fast_timed_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
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

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept
    {
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

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept
    {
        auto c = count_.load( std::memory_order_relaxed );
        while ( c > 0 ) {
            if ( count_.compare_exchange_strong( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

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
                // Timeout — try to undo our registration
                auto restored = count_.fetch_add( 1, std::memory_order_relaxed );
                if ( restored >= 0 ) {
                    // A release happened concurrently and granted us a token.
                    // Our fetch_add(1) undid the registration, but the token was
                    // meant for us. Re-consume it to maintain the invariant.
                    count_.fetch_sub( 1, std::memory_order_relaxed );
                    return true;
                }
                return false;
            }
        }
    }

    template < class Rep, class Period >
    [[nodiscard]] bool try_acquire_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_acquire_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    std::atomic< int32_t > count_;
};

} // namespace nova::sync
