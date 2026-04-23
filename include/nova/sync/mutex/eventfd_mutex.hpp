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


/// @brief Async-capable mutex implemented via Linux `eventfd` with fast user-space path.
///
/// State Layout
/// ------------
/// `state_` is an atomic `uint32_t` with the following encoding:
///
///   - **Bit 0** (lock bit): 1 = locked, 0 = unlocked.
///   - **Bits 1–31** (waiter count): number of registered waiters × 2.
///     Each waiter adds 2 to `state_` so the waiter count occupies the
///     upper 31 bits without interfering with the lock bit.
///
/// The fast path (`lock()` / `try_lock()`) operates entirely in user-space
/// via CAS on bit 0.  No kernel call is made on the uncontended path.
///
/// `unlock()` clears bit 0 via `fetch_and(~1u)`.  If the previous state was
/// `> 1` (at least one waiter registered), it writes a token to the eventfd
/// to wake exactly one waiter.  If no waiters are registered, the kernel is
/// not touched — this is the fast-path optimization.
///
/// Waiter Registration Protocol
/// ----------------------------
/// Any code that plans to wait on the eventfd must call
/// `add_async_waiter()` before monitoring the fd, and either:
///
///   (a) `remove_async_waiter()` if it abandons the wait (timeout /
///       cancellation) without acquiring the lock, or
///
///   (b) atomically decrement the waiter count as part of the CAS that
///       sets the lock bit: `desired = (s - 2) | 1`.  In this case the
///       caller must also call `consume_lock()` to drain any pending
///       eventfd token that `unlock()` may have posted between the
///       waiter registration and the CAS.
///
/// Why `consume_lock()` is Required
/// --------------------------------
/// Consider: waiter registers (`state_ += 2`), then `unlock()` sees
/// `prev > 1` and writes a token.  Before the waiter enters the
/// kernel wait, it re-checks `state_` and finds the lock bit clear —
/// so it grabs ownership via CAS.  The eventfd token is now stale.
///
/// For **event-loop integrations** (Boost.Asio, libdispatch, Qt), the
/// stale token makes the eventfd appear readable.  The event handler fires,
/// calls `try_lock()` (which is a pure CAS — it never reads the eventfd),
/// fails, re-arms, and the fd is *still* readable → immediate re-fire →
/// **busy-loop**.  `consume_lock()` performs a non-blocking `read()` to
/// drain the stale token.
///
/// Async Waiter Integration
/// -------------------------
/// When integrating with event loop systems (Qt, Boost.Asio, etc.), call
/// `add_async_waiter()` before waiting on the `native_handle()` file descriptor,
/// and `remove_async_waiter()` after the wait completes or is cancelled.  This
/// ensures the fast-path optimization remains correct: if any async waiter is
/// pending, `unlock()` will always trigger the eventfd.
///
/// After the event loop detects readability and `try_lock()` succeeds, call
/// `consume_lock()` to drain the stale eventfd token before releasing
/// the waiter registration.  The helper `platform_try_acquire_after_wait()`
/// in `async_support.hpp` does both steps automatically.
///
/// Example
/// -------
/// @code
///   fast_eventfd_mutex mtx;
///   int fd = mtx.native_handle();
///
///   mtx.add_async_waiter();  // Register before waiting on fd
///   // Wait on fd for readability (via select/poll/epoll/Qt/etc.)
///   if (mtx.try_lock()) {
///       mtx.consume_lock();      // Drain stale eventfd token
///       mtx.remove_async_waiter();
///       // ... critical section ...
///       mtx.unlock();
///   } else {
///       // Spurious wakeup — re-arm the wait
///   }
/// @endcode
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

    /// @brief Register an async waiter.
    ///
    /// Increments the waiter count so that `unlock()` writes to the eventfd
    /// even when called from the fast path.  Must be balanced by a call to
    /// `remove_async_waiter()`.
    uint32_t add_async_waiter() noexcept
    {
        return state_.fetch_add( 2u, std::memory_order_relaxed ) + 2u;
    }

    /// @brief Unregister a previously added async waiter.
    void remove_async_waiter() noexcept
    {
        state_.fetch_sub( 2u, std::memory_order_relaxed );
    }

    /// @brief Drain one pending kernel notification (if any) without blocking.
    ///
    /// Must be called after acquiring the lock via user-space CAS while
    /// registered as an async waiter.  In that situation `unlock()` may have
    /// already written an eventfd token (because it saw `state_ > 1`); if
    /// ownership was then obtained through the CAS path rather than by
    /// consuming the token, the stale token remains queued and would cause
    /// a spurious wakeup for the next waiter.
    ///
    /// This is a non-blocking operation (non-blocking `read()` on the eventfd).
    /// Calling it when no token is pending is harmless.
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
