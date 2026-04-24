// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __linux__ )
#    define NOVA_SYNC_HAS_EVENTFD_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX

#    include <atomic>
#    include <chrono>
#    include <nova/sync/detail/backoff.hpp>
#    include <nova/sync/detail/compat.hpp>
#    include <nova/sync/detail/timed_wait.hpp>
#    include <nova/sync/mutex/support/async_waiter_guard.hpp>

namespace nova::sync {

/// @brief Simple async-capable mutex implemented via Linux `eventfd` in semaphore mode.
///
/// Uses the eventfd counter directly as a binary semaphore: the fd starts
/// readable (count=1, unlocked) and `try_lock()` reads one token.  `unlock()`
/// writes one token, waking exactly one `poll()` waiter.
///
/// Example (Simple Poll Loop)
/// --------------------------
/// @code
///   eventfd_mutex mtx;
///   int fd = mtx.native_handle();
///
///   // No waiter registration needed
///   while (true) {
///       if (mtx.try_lock()) {
///           // Critical section
///           mtx.unlock();
///           break;
///       }
///       // Wait for fd readability (via poll/epoll/select)
///       poll(..., fd, POLLIN, ...);
///       // Spurious wakeups possible; will retry try_lock
///   }
/// @endcode
///
class eventfd_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;
    /// @brief Effective timeout resolution (ppoll is nanosecond-precise).
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked eventfd mutex.
    eventfd_mutex();
    ~eventfd_mutex();
    eventfd_mutex( const eventfd_mutex& )            = delete;
    eventfd_mutex& operator=( const eventfd_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept;

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    bool try_lock() noexcept;

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_ns nanoseconds.
    ///
    /// Single ppoll call per attempt — no calls to now().
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    bool try_lock_for( duration_type rel_ns ) noexcept
    {
        if ( rel_ns.count() <= 0 )
            return try_lock();
        while ( !try_lock() ) {
            if ( !detail::ppoll_for( evfd_, rel_ns ) )
                return try_lock(); // one last attempt after timeout
        }
        return true;
    }

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// Casts the duration to nanoseconds and delegates to the nanosecond overload.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        return try_lock_for( std::chrono::duration_cast< duration_type >( rel_time ) );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// Specializes on Clock type to use the optimal ppoll_until overload:
    /// - system_clock → CLOCK_REALTIME timerfd
    /// - steady_clock → CLOCK_MONOTONIC timerfd
    /// - other clocks → fallback to try_lock_for with duration computation
    ///
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time )
    {
        if ( try_lock() )
            return true;

        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            while ( true ) {
                if ( !detail::ppoll_until( evfd_, abs_time ) )
                    return try_lock();
                if ( try_lock() )
                    return true;
            }
        } else {
            // Fallback: use try_lock_for with duration-based approach
            return try_lock_for( std::chrono::duration_cast< duration_type >( abs_time - Clock::now() ) );
        }
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept;

    /// @brief Returns the eventfd file descriptor for async integration.
    ///
    /// When waiting on this file descriptor, it becomes readable when the mutex
    /// is unlocked.  The caller can then attempt `try_lock()` and, if
    /// unsuccessful, re-register for notifications.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return evfd_;
    }

private:
    const int evfd_ { -1 };
};


/// @brief Fast async-capable mutex optimized for event loop integration.
///
/// Provides user-space fast path with kernel fallback for high-contention scenarios.
/// Ideal for integration with event loops (Boost.Asio, Qt, libdispatch).
///
/// To integrate with an event loop:
///  1. Call `add_async_waiter()` before waiting on `native_handle()`.
///  2. After `try_lock()` succeeds, call `consume_lock()`.
///  3. Call `remove_async_waiter()` when done or on timeout.
///
/// Use `async_waiter_guard` for automatic lifetime management.
///
class alignas( detail::hardware_destructive_interference_size ) fast_eventfd_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked fast_eventfd_mutex.
    fast_eventfd_mutex();
    ~fast_eventfd_mutex();
    fast_eventfd_mutex( const fast_eventfd_mutex& )            = delete;
    fast_eventfd_mutex& operator=( const fast_eventfd_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow();
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    bool try_lock() noexcept
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( ( s & 1u ) == 0 ) {
            if ( state_.compare_exchange_weak( s, s | 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept;

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// Casts the duration to nanoseconds and delegates to the private slow path.
    /// Zero calls to now().
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        return try_lock_for_ns( std::chrono::duration_cast< duration_type >( rel_time ) );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// Specializes on Clock type to use the optimal ppoll_until overload:
    /// - system_clock → CLOCK_REALTIME timerfd
    /// - steady_clock → CLOCK_MONOTONIC timerfd
    /// - other clocks → fallback to try_lock_for_ns with duration computation
    ///
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time )
    {
        if ( try_lock() )
            return true;

        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            // Brief spin phase (same as try_lock_for_ns)
            detail::exponential_backoff backoff;
            uint32_t                    s = state_.load( std::memory_order_relaxed );
            while ( backoff.backoff < detail::exponential_backoff::spin_limit ) {
                if ( ( s & 1u ) == 0 ) {
                    if ( state_.compare_exchange_weak( s, s | 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
                        return true;
                    continue;
                }
                backoff.run();
                s = state_.load( std::memory_order_relaxed );
            }

            // Register as waiter before calling ppoll_until
            s = add_async_waiter(); // returns state after +2
            detail::async_waiter_guard< fast_eventfd_mutex > guard( *this, detail::adopt_async_waiter );

            while ( true ) {
                if ( ( s & 1u ) == 0 ) {
                    // Try to acquire — CAS will update `s` on failure.
                    uint32_t desired = ( s - 2u ) | 1u;
                    if ( state_.compare_exchange_weak(
                             s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                        // CAS succeeded: drain any pending eventfd token.
                        consume_lock();
                        guard.dismiss(); // waiter count already decremented in CAS
                        return true;
                    }
                    continue;
                }

                if ( !detail::ppoll_until( evfd_, abs_time ) ) {
                    // Timed out — guard destructor calls remove_async_waiter().
                    return try_lock();
                }

                consume_lock();
                s = state_.load( std::memory_order_acquire );
            }
        } else {
            return try_lock_for( std::chrono::duration_cast< duration_type >( abs_time - Clock::now() ) );
        }
    }

    /// @brief Returns the eventfd file descriptor for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return evfd_;
    }

    /// @brief Register an async waiter before polling the file descriptor.
    ///
    /// Must be paired with `remove_async_waiter()` or by consuming the lock after successful acquisition.
    uint32_t add_async_waiter() noexcept
    {
        return state_.fetch_add( 2u, std::memory_order_relaxed ) + 2u;
    }

    /// @brief Unregister a previously added async waiter.
    void remove_async_waiter() noexcept
    {
        state_.fetch_sub( 2u, std::memory_order_relaxed );
    }

    /// @brief Drain pending kernel notifications after acquiring the lock.
    ///
    /// Call this after `try_lock()` succeeds while registered as an async waiter.
    /// Safe to call when no notifications are pending.
    void consume_lock() const noexcept;

private:
    std::atomic< uint32_t > state_ { 0 }; // Bit 0: locked; Bits 1-31: waiter count
    int                     evfd_ { -1 };

    void lock_slow() noexcept;

    /// Blocking slow path: waits up to rel_ns for the lock.  No calls to now().
    bool try_lock_for_ns( duration_type rel_ns ) noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
