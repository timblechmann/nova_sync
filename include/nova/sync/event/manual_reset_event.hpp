// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>

namespace nova::sync {

/// @brief Manual-reset event.
///
/// Once `signal()` is called, all waiters are woken and subsequent `wait()` /
/// `try_wait()` calls return immediately until `reset()` is called.

class manual_reset_event
{
public:
    /// @brief Constructs the event in the "not set" state.
    explicit manual_reset_event( bool initially_set = false ) noexcept :
        state_( initially_set ? 1u : 0u )
    {}

    ~manual_reset_event()                                      = default;
    manual_reset_event( const manual_reset_event& )            = delete;
    manual_reset_event& operator=( const manual_reset_event& ) = delete;

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

    /// @brief Returns true if the event is currently set, without blocking.
    [[nodiscard]] bool try_wait() const noexcept
    {
        return state_.load( std::memory_order_acquire ) != 0u;
    }

    /// @brief Blocks until the event is set.
    ///
    void wait() noexcept
    {
        if ( state_.load( std::memory_order_acquire ) != 0u )
            return;

        // Park: block until the value is no longer 0.
        // Spurious wakeups are handled by the loop.
        while ( state_.load( std::memory_order_relaxed ) == 0u )
            atomic_wait( state_, 0u, std::memory_order_acquire );
    }

    template < class Clock, class Duration >
    [[nodiscard]] bool try_wait_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
    {
        if ( state_.load( std::memory_order_acquire ) != 0 )
            return true;

        while ( state_.load( std::memory_order_relaxed ) == 0 ) {
            if ( !atomic_wait_until( state_, 0u, abs_time, std::memory_order_acquire ) ) {
                return state_.load( std::memory_order_acquire ) != 0;
            }
        }

        return true;
    }

    template < class Rep, class Period >
    [[nodiscard]] bool try_wait_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
    {
        return try_wait_until( std::chrono::steady_clock::now() + rel_time );
    }

private:
    std::atomic< uint32_t > state_;
};

} // namespace nova::sync
