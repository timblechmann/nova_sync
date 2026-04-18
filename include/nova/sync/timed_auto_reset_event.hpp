// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>
#include <semaphore>

#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Auto-reset event with timed-wait support.
///
/// The event starts in the "not set" state.  signal() delivers exactly one
/// token: if a thread is already blocked in wait(), it is woken and the
/// token is consumed atomically. If no thread is waiting the token is
/// stored and consumed by the next call to wait() or try_wait().
class alignas( detail::hardware_destructive_interference_size ) timed_auto_reset_event
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
    ///
    /// Idempotent when already set: a second signal() without an intervening
    /// wait() is discarded.
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
    bool try_wait() noexcept
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( s >= post_one ) {
            if ( state_.compare_exchange_weak( s, s - post_one, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Blocks until a token is available, then consumes it.
    void wait() noexcept
    {
        if ( try_wait() )
            return;

        state_.fetch_add( 1u, std::memory_order_relaxed );

        uint32_t s = state_.load( std::memory_order_acquire );
        while ( s >= post_one ) {
            if ( state_.compare_exchange_weak(
                     s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                sem_.try_acquire();
                return;
            }
        }

        while ( true ) {
            sem_.acquire();
            s = state_.load( std::memory_order_relaxed );
            while ( true ) {
                if ( s >= post_one ) {
                    if ( state_.compare_exchange_weak(
                             s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) )
                        return;
                } else {
                    break;
                }
            }
        }
    }

    /// @brief Blocks until a token is available or the absolute deadline passes.
    /// @return true if a token was consumed, false if the deadline passed.
    template < class Clock, class Duration >
    bool wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( try_wait() )
            return true;

        state_.fetch_add( 1u, std::memory_order_relaxed );

        uint32_t s = state_.load( std::memory_order_acquire );
        while ( s >= post_one ) {
            if ( state_.compare_exchange_weak(
                     s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                sem_.try_acquire();
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
    bool wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return wait_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    bool on_timed_wait_timeout() noexcept
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( true ) {
            if ( s >= post_one ) {
                if ( state_.compare_exchange_weak(
                         s, s - 1u - post_one, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                    sem_.acquire();
                    return true;
                }
            } else {
                if ( state_.compare_exchange_weak( s, s - 1u, std::memory_order_relaxed, std::memory_order_relaxed ) )
                    return false;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Data

    std::atomic< uint32_t >                state_;
    std::counting_semaphore< max_waiters > sem_ { 0 };
};

} // namespace nova::sync
