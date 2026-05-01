// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <semaphore>
#include <tuple> // std::ignore

#include <nova/sync/detail/compat.hpp>

#include "nova/sync/event/parking_auto_reset_event.hpp"

namespace nova::sync {
namespace impl {

/// @brief Auto-reset event with timed-wait support.
///
/// Each `signal()` delivers exactly one token. If a thread is blocked in
/// `wait()`, it is woken and the token is consumed. Otherwise the token is
/// stored for the next `wait()` / `try_wait()` call.
class timed_auto_reset_event
{
    // State encoding helpers
    //   bits 0-15  : waiter_count (number of threads waiting)
    //   bits 16-31 : post_count (number of unmatched signals)
    static constexpr std::ptrdiff_t max_waiters = ( std::ptrdiff_t { 1 } << 16 ) - 2;
    static constexpr uint32_t       waiter_mask = ( 1u << 16 ) - 1u;
    static constexpr uint32_t       post_one    = 1u << 16;

public:
    /// @brief Constructs the event.
    explicit timed_auto_reset_event( bool initially_set = false ) noexcept :
        state_( initially_set ? post_one : 0u )
    {}

    ~timed_auto_reset_event()                                          = default;
    timed_auto_reset_event( const timed_auto_reset_event& )            = delete;
    timed_auto_reset_event& operator=( const timed_auto_reset_event& ) = delete;

    /// @brief Delivers one token, waking exactly one waiter.
    void signal() noexcept
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( true ) {
            const uint32_t post_count   = s >> 16;
            const uint32_t waiter_count = s & waiter_mask;

            if ( post_count > waiter_count )
                return;

            if ( state_.compare_exchange_weak( s, s + post_one, std::memory_order_release, std::memory_order_relaxed ) ) {
                if ( waiter_count > post_count )
                    sem_.release( 1 );
                return;
            }
        }
    }

    /// @brief Atomically consumes a token if one is pending.
    /// @return true if a token was available and consumed, false otherwise.
    [[nodiscard]] bool try_wait() noexcept
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( s >= post_one ) {
            if ( state_.compare_exchange_weak( s, s - post_one, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Blocks until a token is available, then consumes it.
    void wait() noexcept;

    /// @brief Blocks until a token is available or the absolute deadline passes.
    /// @return true if a token was consumed, false if the deadline passed.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( try_wait() )
            return true;

        state_.fetch_add( 1u, std::memory_order_relaxed );

        uint32_t s = state_.load( std::memory_order_acquire );
        while ( s >= post_one ) {
            if ( state_.compare_exchange_weak(
                     s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                std::ignore = sem_.try_acquire();
                return true;
            }
        }

        while ( true ) {
            if ( !sem_.try_acquire_until( abs_time ) )
                return on_timed_wait_timeout();

            s = state_.load( std::memory_order_relaxed );
            while ( true ) {
                if ( s >= post_one ) {
                    if ( state_.compare_exchange_weak(
                             s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) )
                        return true;
                } else {
                    break;
                }
            }
        }
    }

    /// @brief Blocks until a token is available or the timeout expires.
    /// @return true if a token was consumed, false if the timeout expired.
    template < class Rep, class Period >
    [[nodiscard]] bool try_wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_wait_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    bool on_timed_wait_timeout() noexcept;

    // -----------------------------------------------------------------------
    // Data

    std::atomic< uint32_t >                state_;
    std::counting_semaphore< max_waiters > sem_ { 0 };
};

} // namespace impl

#if defined( __linux__ ) || defined( _WIN32 )

using timed_auto_reset_event = auto_reset_event;

#else

using timed_auto_reset_event = impl::timed_auto_reset_event;

#endif


} // namespace nova::sync
