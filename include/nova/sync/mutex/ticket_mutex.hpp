// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <chrono>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/futex/atomic_wait.hpp>
#include <nova/sync/mutex/policies.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

/// @brief Fair FIFO ticket mutex with optional exponential backoff.
///
/// Threads acquire tickets in order; each `unlock()` serves the next ticket,
/// guaranteeing strict FIFO ordering for `lock()` callers.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy         | Effect                                                             |
/// |----------------|--------------------------------------------------------------------|
/// | `with_backoff` | Spin with exponential backoff before sleeping on the futex.        |
///
/// Without `with_backoff`, threads sleep on the futex immediately after taking a ticket.
///
/// @note `try_lock_for` / `try_lock_until` do **not** acquire a ticket and therefore do
///       not participate in FIFO order. They opportunistically retry `try_lock()` until
///       the deadline, sleeping between attempts. Timed waiters are not strictly fair
///       against blocked `lock()` callers.
template < typename... Policies >
    requires( parameter::valid_parameters< detail::backoff_allowed_tags, Policies... > )
class NOVA_SYNC_CAPABILITY( "mutex" ) ticket_mutex
{
    alignas( detail::hardware_destructive_interference_size ) std::atomic< uint32_t > serving_ticket_ { 0 };
    alignas( detail::hardware_destructive_interference_size ) std::atomic< uint32_t > next_ticket_ { 0 };

    static constexpr bool use_backoff = detail::has_backoff_v< Policies... >;

public:
    ticket_mutex()                                 = default;
    ~ticket_mutex()                                = default;
    ticket_mutex( const ticket_mutex& )            = delete;
    ticket_mutex& operator=( const ticket_mutex& ) = delete;

    /// @brief Acquires the lock in FIFO ticket order.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t my_ticket = next_ticket_.fetch_add( 1, std::memory_order_relaxed );

        if ( serving_ticket_.load( std::memory_order_acquire ) == my_ticket )
            return;

        lock_slow( my_ticket );
    }

    /// @brief Attempts to acquire the lock without waiting.
    /// @return `true` if acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t current_serving = serving_ticket_.load( std::memory_order_acquire );
        uint32_t expected        = current_serving;
        return next_ticket_.compare_exchange_strong( expected,
                                                     current_serving + 1,
                                                     std::memory_order_acquire,
                                                     std::memory_order_relaxed );
    }

    /// @brief Releases the lock and wakes the next waiting thread.
    void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        uint32_t next_serving = serving_ticket_.load( std::memory_order_relaxed ) + 1;
        serving_ticket_.store( next_serving, std::memory_order_release );
        atomic_notify_all( serving_ticket_ );
    }

    /// @brief Attempts to acquire the lock within a relative timeout.
    ///
    /// Does not take a ticket; see class documentation for fairness notes.
    /// @return `true` if acquired, `false` if timed out.
    template < class Rep, class Period >
    [[nodiscard]] bool try_lock_for( std::chrono::duration< Rep, Period > const& rel_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    /// @brief Attempts to acquire the lock until an absolute deadline.
    ///
    /// Does not take a ticket; see class documentation for fairness notes.
    /// @return `true` if acquired, `false` if timed out.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_lock_until( std::chrono::time_point< Clock, Duration > const& abs_time ) noexcept
        NOVA_SYNC_TRY_ACQUIRE( true )
    {
        while ( true ) {
            if ( try_lock() )
                return true;

            uint32_t serving = serving_ticket_.load( std::memory_order_relaxed );

            if ( Clock::now() >= abs_time )
                return false;

            atomic_wait_until( serving_ticket_, serving, abs_time );
        }
    }

private:
    NOVA_SYNC_NOINLINE void lock_slow( uint32_t my_ticket ) noexcept
    {
        if constexpr ( use_backoff )
            lock_slow_backoff( my_ticket );
        else
            lock_slow_plain( my_ticket );
    }

    void lock_slow_plain( uint32_t my_ticket ) noexcept;
    void lock_slow_backoff( uint32_t my_ticket ) noexcept;
};

} // namespace nova::sync
