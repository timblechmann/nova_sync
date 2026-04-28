// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <nova/sync/detail/compat.hpp>

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
            for ( int32_t i = 0; i < to_wake; ++i )
                count_.notify_one();
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
            count_.wait( c, std::memory_order_relaxed );
        }
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

private:
    std::atomic< int32_t > count_;
};

} // namespace nova::sync
