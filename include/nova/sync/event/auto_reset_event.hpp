// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>

#include <nova/sync/detail/backoff.hpp>
#include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Auto-reset event.
///
/// Each `signal()` delivers exactly one token. If a thread is blocked in
/// `wait()`, it is woken and the token is consumed. Otherwise the token is
/// stored for the next `wait()` / `try_wait()` call.
class alignas( detail::hardware_destructive_interference_size ) auto_reset_event
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
                    state_.notify_all();
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
            state_.wait( cur, std::memory_order_relaxed );
            cur = state_.load( std::memory_order_relaxed );
        }

        std::atomic_thread_fence( std::memory_order_acquire );
    }

private:
    std::atomic< int32_t > state_;
};

} // namespace nova::sync
