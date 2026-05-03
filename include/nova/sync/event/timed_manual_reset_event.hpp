// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <semaphore>
#include <tuple> // std::ignore

#include <nova/sync/detail/compat.hpp>

#include "nova/sync/event/parking_manual_reset_event.hpp"

namespace nova::sync {

/// @brief Manual-reset event with timed-wait support.
///
/// Once `signal()` is called, all waiters are woken and subsequent `wait()` /
/// `try_wait()` calls return immediately until `reset()` is called.

namespace impl {

class timed_manual_reset_event
{
    // -----------------------------------------------------------------------
    // State encoding helpers
    //   state_ = (waiter_count << 1) | flag_bit
    static constexpr std::ptrdiff_t max_waiters = ( std::ptrdiff_t { 1 } << 30 ) - 1;
    static constexpr uint32_t       flag_bit    = 1u;
    static constexpr uint32_t       waiter_one  = 2u; // adding one waiter

public:
    /// @brief Constructs the event.
    explicit timed_manual_reset_event( bool initially_set = false ) noexcept :
        state_( initially_set ? flag_bit : 0u )
    {}

    ~timed_manual_reset_event()                                            = default;
    timed_manual_reset_event( const timed_manual_reset_event& )            = delete;
    timed_manual_reset_event& operator=( const timed_manual_reset_event& ) = delete;

    // -----------------------------------------------------------------------
    // Signalling

    /// @brief Transitions the event to "set", waking all waiters.
    void signal() noexcept
    {
        const uint32_t prev = state_.fetch_or( flag_bit, std::memory_order_release );
        if ( prev & flag_bit )
            return;

        const uint32_t wc = prev >> 1;
        if ( wc > 0 )
            sem_.release( std::ptrdiff_t( wc ) );
    }

    /// @brief Transitions the event back to "not set".
    void reset() noexcept
    {
        state_.fetch_and( ~flag_bit, std::memory_order_relaxed );
    }

    // -----------------------------------------------------------------------
    // Waiting

    /// @brief Returns true if the event is currently set, without blocking.
    [[nodiscard]] bool try_wait() const noexcept
    {
        return ( state_.load( std::memory_order_acquire ) & flag_bit ) != 0u;
    }

    /// @brief Blocks until the event is set.
    void wait() noexcept
    {
        if ( state_.load( std::memory_order_acquire ) & flag_bit )
            return; // fast path

        state_.fetch_add( waiter_one, std::memory_order_relaxed );

        if ( state_.load( std::memory_order_acquire ) & flag_bit ) {
            state_.fetch_sub( waiter_one, std::memory_order_relaxed );
            std::ignore = sem_.try_acquire();
            return;
        }

        while ( true ) {
            sem_.acquire();
            if ( state_.load( std::memory_order_acquire ) & flag_bit ) {
                state_.fetch_sub( waiter_one, std::memory_order_relaxed );
                return;
            }
        }
    }

    /// @brief Blocks until the event is set or the absolute deadline passes.
    /// @return true if the event was set, false if the deadline passed.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( state_.load( std::memory_order_acquire ) & flag_bit )
            return true;

        state_.fetch_add( waiter_one, std::memory_order_relaxed );

        if ( state_.load( std::memory_order_acquire ) & flag_bit ) {
            state_.fetch_sub( waiter_one, std::memory_order_relaxed );
            std::ignore = sem_.try_acquire();
            return true;
        }

        while ( true ) {
            if ( !sem_.try_acquire_until( abs_time ) )
                return on_timed_wait_timeout();

            if ( state_.load( std::memory_order_acquire ) & flag_bit ) {
                state_.fetch_sub( waiter_one, std::memory_order_relaxed );
                return true;
            }
        }
    }

    /// @brief Blocks until the event is set or the timeout expires.
    /// @return true if the event was set, false if the timeout expired.
    template < class Rep, class Period >
    [[nodiscard]] bool try_wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_wait_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    bool on_timed_wait_timeout() noexcept;

    // -----------------------------------------------------------------------
    // Data

    // state:
    //   bit  0      : signal flag  (1 = set, 0 = not set)
    //   bits 1–31   : waiter count (number of threads registered for semaphore wakeup)
    //                 stored as (count << 1) so that bit 0 is the flag.

    std::atomic< uint32_t >                state_;
    std::counting_semaphore< max_waiters > sem_ { 0 };
};

} // namespace impl

#if defined( __linux__ ) || defined( _WIN32 )

using timed_manual_reset_event = manual_reset_event;

#else

using timed_manual_reset_event = impl::timed_manual_reset_event;

#endif


} // namespace nova::sync
