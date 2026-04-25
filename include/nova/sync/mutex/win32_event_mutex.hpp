// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#    define NOVA_SYNC_HAS_WIN32_EVENT_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_EVENT_MUTEX

#    include <atomic>
#    include <chrono>
#    include <nova/sync/detail/timed_wait.hpp>
#    include <nova/sync/mutex/annotations.hpp>

typedef void* HANDLE;

namespace nova::sync {

/// @brief Async-capable mutex using Win32 synchronization primitives.
///
/// Implemented as a counting semaphore (max 1) for non-recursive mutual
/// exclusion, plus an auto-reset event used to notify async waiters.
/// The timed variants delegate to detail::wait_handle_until / wait_handle_for,
/// which use a waitable timer alongside the lock handle so the wait can be
/// interrupted as soon as the lock is released rather than polling.
class NOVA_SYNC_CAPABILITY( "mutex" ) win32_event_mutex
{
public:
    /// @brief The native handle type — a Win32 HANDLE.
    using native_handle_type = HANDLE;

    /// @brief Constructs an unlocked win32_event_mutex.
    win32_event_mutex();
    ~win32_event_mutex();
    win32_event_mutex( const win32_event_mutex& )            = delete;
    win32_event_mutex& operator=( const win32_event_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept NOVA_SYNC_ACQUIRE();

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true );

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// Converts the relative duration to a steady_clock deadline and delegates
    /// to try_lock_until so a single waitable-timer wait is used.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        using namespace std::chrono_literals;

        if ( rel_time <= 0ms )
            return try_lock();
        return try_lock_until( std::chrono::steady_clock::now() + rel_time );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// For system_clock and steady_clock the implementation creates a waitable
    /// timer that fires at the deadline and waits on both the lock handle and the
    /// timer simultaneously, so the thread wakes as soon as the lock becomes
    /// available — not just at the next timer tick.  For other clocks the
    /// remaining duration is computed and passed to wait_handle_for.
    ///
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            // Check if deadline has already passed.
            if ( Clock::now() >= abs_time )
                return false;

            // Wait for the lock handle to be signaled or the deadline to fire.
            // WaitForMultipleObjects on a semaphore handle atomically acquires it,
            // so returning true here means we hold the lock.
            if ( detail::wait_handle_until( handle_, abs_time ) )
                return true;
            return false;
        } else {
            // Unknown clock: compute remaining time and use the relative wait.
            return try_lock_for( abs_time - Clock::now() );
        }
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept NOVA_SYNC_RELEASE();

    /// @brief Returns the wakeup event HANDLE for async integration.
    ///
    /// This is an auto-reset event that is `SetEvent`d by `unlock()` when there
    /// are registered async waiters. Async waiters should watch this handle via
    /// `object_handle::async_wait`, then call `try_lock()` in the callback to
    /// actually acquire the semaphore.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return event_;
    }

    /// @brief Register an async waiter before waiting on the native handle.
    void add_async_waiter() noexcept NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        state_.fetch_add( 2u, std::memory_order_relaxed );
    }

    /// @brief Unregister a previously added async waiter.
    void remove_async_waiter() noexcept NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        state_.fetch_sub( 2u, std::memory_order_relaxed );
    }

    /// @brief Drain pending kernel notifications after acquiring the lock.
    ///
    /// Call this after successfully acquiring the lock while registered as an
    /// async waiter.  Safe to call when no notifications are pending.
    void consume_lock() const noexcept NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS;

private:
    HANDLE                  handle_ { nullptr }; ///< Semaphore (max count 1) — the lock itself.
    std::atomic< uint32_t > state_ { 0 };        ///< Bits 1+: async waiter count.
    HANDLE                  event_ { nullptr };  ///< Auto-reset event for async wakeup.
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_EVENT_MUTEX
