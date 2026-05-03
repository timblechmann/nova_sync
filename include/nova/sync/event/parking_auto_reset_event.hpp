// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>
#include <nova/sync/mutex/policies.hpp>

namespace nova::sync {

/// @brief Auto-reset event with optional exponential backoff.
///
/// Each `signal()` delivers exactly one token. If a thread is blocked in
/// `wait()`, it is woken and the token is consumed. Otherwise the token is
/// stored for the next `wait()` / `try_wait()` call.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy        | Effect                                                 |
/// |---------------|--------------------------------------------------------|
/// | (no exponential_backoff)  | Park immediately when no token available (default).    |
/// | `with_backoff`| Spin with exponential backoff before parking.          |
///
/// ### Aliases
/// - `parking_auto_reset_event<>`             — pure park, no spinning.
/// - `parking_auto_reset_event<with_backoff>` — spin-then-park.
/// - `auto_reset_event`                       — deprecated alias for `parking_auto_reset_event<>`.
template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
class parking_auto_reset_event
{
    std::atomic< int32_t > state_;

    static constexpr bool use_backoff = detail::has_backoff_v< Policies... >;

public:
    /// @brief Constructs the event.
    /// @param initially_set  When true the first wait() / try_wait() succeeds without blocking.
    explicit parking_auto_reset_event( bool initially_set = false ) noexcept :
        state_( initially_set ? 1 : 0 )
    {}

    ~parking_auto_reset_event()                                            = default;
    parking_auto_reset_event( const parking_auto_reset_event& )            = delete;
    parking_auto_reset_event& operator=( const parking_auto_reset_event& ) = delete;

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

    /// @brief Atomically consumes the signal if set.
    /// @return `true` if a signal was available and consumed, `false` otherwise.
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
    void wait() noexcept
    {
        if constexpr ( use_backoff ) {
            detail::exponential_backoff backoff;
            while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
                if ( try_wait() )
                    return;
                backoff.run();
            }
        }

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

    /// @brief Blocks until a signal is available or the deadline is reached.
    /// @return `true` if signal consumed, `false` if timed out.
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
                cur = state_.load( std::memory_order_relaxed );
                if ( cur > my_slot )
                    return true;
                state_.fetch_add( 1, std::memory_order_relaxed );
                return false;
            }
            cur = state_.load( std::memory_order_relaxed );
        }

        return true;
    }

    /// @brief Blocks until a signal is available or the duration expires.
    /// @return `true` if signal consumed, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_wait_until( std::chrono::steady_clock::now() + rel_time );
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Convenience alias

/// @brief Deprecated alias for `parking_auto_reset_event<>`.
using auto_reset_event = parking_auto_reset_event<>;

} // namespace nova::sync
