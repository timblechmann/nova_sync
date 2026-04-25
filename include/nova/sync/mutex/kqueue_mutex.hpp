// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( __APPLE__ )
#    define NOVA_SYNC_HAS_KQUEUE_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX

#    include <atomic>
#    include <chrono>
#    include <nova/sync/detail/compat.hpp>
#    include <nova/sync/detail/timed_wait.hpp>
#    include <nova/sync/mutex/support/async_waiter_guard.hpp>

namespace nova::sync {

/// @brief Simple async-capable mutex implemented via Apple `kqueue` with `EVFILT_USER`.
///
/// Example (Simple Kevent Loop)
/// ----------------------------
/// @code
///   kqueue_mutex mtx;
///   int fd = mtx.native_handle();
///
///   // No waiter registration needed
///   while (true) {
///       if (mtx.try_lock()) {
///           // Critical section
///           mtx.unlock();
///           break;
///       }
///       // Wait for fd readability (via poll)
///       poll(..., fd, POLLIN, ...);
///       // Spurious wakeups possible; will retry try_lock
///   }
/// @endcode
///
class kqueue_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;
    /// @brief Effective timeout resolution (kevent timespec is nanosecond-precise).
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked kqueue mutex.
    kqueue_mutex();
    ~kqueue_mutex();
    kqueue_mutex( const kqueue_mutex& )            = delete;
    kqueue_mutex& operator=( const kqueue_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept;

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept;

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_ns nanoseconds.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    bool try_lock_for( duration_type rel_ns ) noexcept
    {
        if ( rel_ns.count() <= 0 )
            return try_lock();
        while ( !try_lock() ) {
            if ( !detail::kevent_for( kqfd_, rel_ns ) )
                return try_lock(); // one last attempt after timeout
            // Event fired - kevent_for's kevent call consumed the event (EV_CLEAR cleared it).
            // This consumption represents the lock acquisition, so return true.
            return true;
        }
        return true;
    }

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        return try_lock_for( std::chrono::duration_cast< duration_type >( rel_time ) );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
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
                if ( !detail::kevent_until( kqfd_, 1, abs_time ) )
                    return try_lock(); // one last attempt after timeout
                return true;
            }
        } else {
            return try_lock_until_fallback( abs_time - Clock::now() );
        }
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept;

    /// @brief Returns the kqueue file descriptor for async integration.
    ///
    /// When waiting on this file descriptor, it becomes readable when the mutex is unlocked.
    /// The caller can then attempt to acquire the lock (e.g. via `try_lock()`) and, if
    /// unsuccessful, re-register for notifications.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
    }

private:
    const int kqfd_;
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
/// Example
/// -------
/// @code
///   fast_kqueue_mutex mtx;
///   int fd = mtx.native_handle();
///
///   mtx.add_async_waiter();  // Register before waiting on fd
///   // Wait on fd for events (via kevent/libdispatch/Qt/Asio/etc.)
///   if (mtx.try_lock()) {
///       mtx.consume_lock();      // Drain kernel notification
///       mtx.remove_async_waiter();
///       // ... critical section ...
///       mtx.unlock();
///   } else {
///       // Spurious wakeup — re-arm the wait
///   }
/// @endcode
///
class alignas( detail::hardware_destructive_interference_size ) fast_kqueue_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;
    /// @brief Effective timeout resolution (kevent timespec is nanosecond-precise).
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked fast_kqueue_mutex.
    fast_kqueue_mutex();
    ~fast_kqueue_mutex();
    fast_kqueue_mutex( const fast_kqueue_mutex& )            = delete;
    fast_kqueue_mutex& operator=( const fast_kqueue_mutex& ) = delete;

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
    [[nodiscard]] bool try_lock() noexcept
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
        return try_lock_for_impl( rel_time );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// Specializes on Clock type to use the optimal kevent_until overload:
    /// - system_clock → uses system time-based timer event
    /// - steady_clock → uses steady time-based timer event
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
            // Register as async waiter so unlock() will trigger the kevent.
            auto                                            s = add_async_waiter();
            detail::async_waiter_guard< fast_kqueue_mutex > guard( *this, detail::adopt_async_waiter );

            while ( true ) {
                if ( ( s & 1u ) == 0 ) {
                    uint32_t desired = ( s - 2u ) | 1u;
                    if ( state_.compare_exchange_weak(
                             s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                        // CAS succeeded: consume any pending NOTE_TRIGGER.
                        consume_lock();
                        guard.dismiss(); // waiter count already decremented in CAS
                        return true;
                    }
                    continue;
                }

                if ( !detail::kevent_until( kqfd_, 1, abs_time ) )
                    // Timed out — guard destructor calls remove_async_waiter().
                    return try_lock(); // one last attempt after timeout

                consume_lock();
                s = state_.load( std::memory_order_acquire );
            }
        } else {
            return try_lock_for( abs_time - Clock::now() );
        }
    }

    /// @brief Returns the kqueue file descriptor for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
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
    const int               kqfd_ { -1 };

    void lock_slow() noexcept;

    bool try_lock_for_impl( std::chrono::nanoseconds ) noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_MUTEX
