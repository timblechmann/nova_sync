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

/// @brief Manual-reset event with optional exponential backoff.
///
/// Once `signal()` is called, all waiters are woken and subsequent `wait()` /
/// `try_wait()` calls return immediately until `reset()` is called.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy        | Effect                                                 |
/// |---------------|--------------------------------------------------------|
/// | (no exponential_backoff)  | Park immediately when not set (default).               |
/// | `with_backoff`| Spin with exponential backoff before parking.          |
///
/// ### Aliases
/// - `parking_manual_reset_event<>`             — pure park, no spinning.
/// - `parking_manual_reset_event<with_backoff>` — spin-then-park.
/// - `manual_reset_event`                       — deprecated alias for `parking_manual_reset_event<>`.
template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
class parking_manual_reset_event
{
    std::atomic< uint32_t > state_;

    static constexpr bool use_backoff = detail::has_backoff_v< Policies... >;

public:
    /// @brief Constructs the event in the "not set" state.
    explicit parking_manual_reset_event( bool initially_set = false ) noexcept :
        state_( initially_set ? 1u : 0u )
    {}

    ~parking_manual_reset_event()                                              = default;
    parking_manual_reset_event( const parking_manual_reset_event& )            = delete;
    parking_manual_reset_event& operator=( const parking_manual_reset_event& ) = delete;

    /// @brief Transitions the event to "set", waking all waiters.
    void signal() noexcept
    {
        if ( state_.exchange( 1u, std::memory_order_release ) == 0u )
            atomic_notify_all( state_ );
    }

    /// @brief Transitions the event back to "not set".
    void reset() noexcept
    {
        state_.store( 0u, std::memory_order_relaxed );
    }

    /// @brief Returns `true` if the event is currently set, without blocking.
    [[nodiscard]] bool try_wait() const noexcept
    {
        return state_.load( std::memory_order_acquire ) != 0u;
    }

    /// @brief Blocks until the event is set.
    void wait() noexcept
    {
        if constexpr ( use_backoff ) {
            detail::exponential_backoff backoff;
            while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
                if ( state_.load( std::memory_order_acquire ) != 0u )
                    return;
                backoff.run();
            }
        }

        if ( state_.load( std::memory_order_acquire ) != 0u )
            return;

        while ( state_.load( std::memory_order_relaxed ) == 0u )
            atomic_wait( state_, 0u, std::memory_order_acquire );
    }

    /// @brief Blocks until the event is set or the deadline is reached.
    /// @return `true` if set, `false` if timed out.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( state_.load( std::memory_order_acquire ) != 0 )
            return true;

        while ( state_.load( std::memory_order_relaxed ) == 0 ) {
            if ( !atomic_wait_until( state_, 0u, abs_time, std::memory_order_acquire ) )
                return state_.load( std::memory_order_acquire ) != 0;
        }

        return true;
    }

    /// @brief Blocks until the event is set or the duration expires.
    /// @return `true` if set, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_wait_until( std::chrono::steady_clock::now() + rel_time );
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Convenience alias

/// @brief Deprecated alias for `parking_manual_reset_event<>`.
using manual_reset_event = parking_manual_reset_event<>;

} // namespace nova::sync
