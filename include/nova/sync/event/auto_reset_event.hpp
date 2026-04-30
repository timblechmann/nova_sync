// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>

namespace nova::sync {

/// @brief Auto-reset event.
///
/// Each `signal()` delivers exactly one token. If a thread is blocked in
/// `wait()`, it is woken and the token is consumed. Otherwise the token is
/// stored for the next `wait()` / `try_wait()` call.
class auto_reset_event
{
public:
    /// @brief Constructs the event.
    /// @param initially_set  When true the first wait() / try_wait() will
    ///                       succeed without blocking.
    explicit auto_reset_event( bool initially_set = false ) noexcept :
        state_( initially_set ? 1 : 0 )
    {}

    ~auto_reset_event()                                    = default;
    auto_reset_event( const auto_reset_event& )            = delete;
    auto_reset_event& operator=( const auto_reset_event& ) = delete;

    // -----------------------------------------------------------------------
    // Signalling

    /// @brief Delivers one token, waking exactly one waiter.
    void signal() noexcept
    {
        int32_t s = state_.load( std::memory_order_relaxed );
        while ( true ) {
            if ( s >= 1 )
                return; // already signalled

            if ( state_.compare_exchange_weak( s, s + 1, std::memory_order_release, std::memory_order_relaxed ) ) {
                if ( s < 0 )
                    atomic_notify_all( state_ );
                return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Waiting

    /// @brief Atomically consumes the signal if set.
    /// @return true if a signal was available and consumed, false otherwise.
    [[nodiscard]] bool try_wait() noexcept
    {
        int32_t s = state_.load( std::memory_order_relaxed );
        while ( s > 0 ) {
            if ( state_.compare_exchange_weak( s, s - 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Blocks until a signal is available, then consumes it.
    ///
    void wait() noexcept
    {
        // Fast path: consume an existing signal.
        int32_t prev = state_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return;

        int32_t my_slot = prev - 1;
        int32_t cur     = state_.load( std::memory_order_relaxed );

        while ( cur <= my_slot ) {
            atomic_wait( state_, cur, std::memory_order_acquire );
            cur = state_.load( std::memory_order_relaxed );
        }
    }

    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( try_wait() )
            return true;

        int32_t prev = state_.fetch_sub( 1, std::memory_order_acquire );
        if ( prev > 0 )
            return true;

        int32_t my_slot = prev - 1;
        int32_t cur     = state_.load( std::memory_order_relaxed );

        while ( cur <= my_slot ) {
            if ( !atomic_wait_until( state_, cur, abs_time, std::memory_order_acquire ) ) {
                // Timeout — but check one more time
                cur = state_.load( std::memory_order_relaxed );
                if ( cur > my_slot ) {
                    return true;
                }
                // Must undo our wait registration: add 1 back
                state_.fetch_add( 1, std::memory_order_relaxed );
                return false;
            }
            cur = state_.load( std::memory_order_relaxed );
        }

        return true;
    }

    template < class Rep, class Period >
    [[nodiscard]] bool try_wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_wait_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    std::atomic< int32_t > state_;
};

} // namespace nova::sync
