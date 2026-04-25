// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/detail/compat.hpp>

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
            state_.notify_all();
    }

    /// @brief Transitions the event back to "not set".
    void reset() noexcept
    {
        state_.store( 0u, std::memory_order_release );
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
            state_.wait( 0u, std::memory_order_relaxed );

        std::atomic_thread_fence( std::memory_order_acquire );
    }

private:
    std::atomic< uint32_t > state_;
};

} // namespace nova::sync
