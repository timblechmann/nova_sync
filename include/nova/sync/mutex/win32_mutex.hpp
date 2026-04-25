// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#    define NOVA_SYNC_HAS_WIN32_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX

#    include <chrono>
#    include <nova/sync/detail/timed_wait.hpp>
#    include <nova/sync/mutex/annotations.hpp>

typedef void* HANDLE;

namespace nova::sync {

/// @brief Async-capable mutex implemented via a Win32 semaphore kernel object.
///
/// A counting semaphore (max 1) is used rather than CreateMutex so that the
/// mutex is non-recursive: a thread that already holds the lock will block
/// (or fail try_lock) on a second acquisition attempt, matching the standard
/// C++ Mutex requirements.
///
/// The timed variants delegate to detail::wait_handle_until / wait_handle_for,
/// which use a waitable timer alongside the lock handle so the thread wakes as
/// soon as the lock is released rather than spinning or relying on millisecond
/// timer ticks.
class NOVA_SYNC_CAPABILITY( "mutex" ) win32_mutex
{
public:
    /// @brief The native handle type — a Win32 HANDLE.
    using native_handle_type = HANDLE;
    /// @brief Effective timeout resolution — 100 ns (Windows FILETIME granularity).
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked win32_mutex.
    win32_mutex();
    ~win32_mutex();
    win32_mutex( const win32_mutex& )            = delete;
    win32_mutex& operator=( const win32_mutex& ) = delete;

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

    /// @brief Returns the HANDLE for async integration.
    ///
    /// When waiting on this handle, the mutex will be signaled when it becomes available.
    /// The caller can then attempt to acquire the lock (e.g. via `try_lock()`) and, if
    /// unsuccessful, re-register for notifications.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
    }

private:
    HANDLE handle_ { nullptr };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_MUTEX
