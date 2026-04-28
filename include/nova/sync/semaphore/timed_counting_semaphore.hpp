// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <semaphore>

#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// Counting semaphore with timed-wait support. Uses `std::counting_semaphore`
/// internally for timed operations.
class timed_counting_semaphore
{
    static constexpr std::ptrdiff_t max_count = ( std::ptrdiff_t { 1 } << 30 ) - 1;

public:
    /// Constructs a semaphore with @p initial tokens (default: 0).
    explicit timed_counting_semaphore( std::ptrdiff_t initial = 0 ) noexcept :
        count_( int32_t( initial ) )
    {
        assert( initial >= 0 && "timed_counting_semaphore: initial count must be non-negative" );
    }

    ~timed_counting_semaphore()                                            = default;
    timed_counting_semaphore( const timed_counting_semaphore& )            = delete;
    timed_counting_semaphore& operator=( const timed_counting_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept
    {
        assert( n >= 0 && "timed_counting_semaphore::release: n must be non-negative" );
        auto prev = count_.fetch_add( int32_t( n ), std::memory_order_release );
        if ( prev < 0 ) {
            auto to_wake = std::min( int32_t( n ), -prev );
            for ( int32_t i = 0; i < to_wake; ++i )
                sem_.release( 1 );
        }
    }

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept
    {
        auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return;

        sem_.acquire();
    }

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept
    {
        auto c = count_.load( std::memory_order_relaxed );
        while ( c > 0 ) {
            if ( count_.compare_exchange_weak( c, c - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// Blocks until a token is available or the deadline passes.
    /// Returns `true` if acquired, `false` if timed out.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_acquire_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( try_acquire() )
            return true;

        auto prev = count_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return true;

        while ( true ) {
            if ( !sem_.try_acquire_until( abs_time ) ) {
                auto c = count_.fetch_add( 1, std::memory_order_relaxed );
                if ( c >= 0 ) {
                    std::ignore = sem_.try_acquire();
                    return true;
                }
                return false;
            }
            return true;
        }
    }

    /// Blocks until a token is available or the timeout expires.
    /// Returns `true` if acquired, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_acquire_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_acquire_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    std::atomic< int32_t >               count_;
    std::counting_semaphore< max_count > sem_ { 0 };
};

} // namespace nova::sync
