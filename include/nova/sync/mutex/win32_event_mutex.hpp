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
class NOVA_SYNC_CAPABILITY( "mutex" ) win32_event_mutex
{
public:
    /// @brief The native handle type — a Win32 HANDLE.
    using native_handle_type = HANDLE;
    /// @brief Effective timeout resolution (WaitForSingleObject is millisecond-granularity).
    using duration_type      = std::chrono::milliseconds;

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

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_ms milliseconds.
    ///
    /// Single WaitForSingleObject call per attempt — no calls to now().
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    bool try_lock_for( duration_type rel_ms ) noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( rel_ms.count() <= 0 )
            return try_lock();
        while ( !try_lock() ) {
            if ( !detail::wait_handle_for( handle_, rel_ms ) )
                return try_lock(); // one last attempt after timeout
        }
        return true;
    }

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// Ceiling-rounds the duration to milliseconds and delegates.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        // Ceiling-round to milliseconds so we never under-wait.
        auto                ns        = std::chrono::duration_cast< std::chrono::nanoseconds >( rel_time );
        constexpr long long ns_per_ms = 1'000'000LL;
        auto                rel_ms    = duration_type { ( ns.count() + ns_per_ms - 1 ) / ns_per_ms };
        return try_lock_for( rel_ms );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// For system_clock and steady_clock, dispatches to platform-specific wait_handle_until
    /// overloads that use native timer handles.  For other clocks, falls back to try_lock_for
    /// with deadline-computed remaining duration.
    ///
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_until_impl( abs_time,
                                    std::is_same< Clock, std::chrono::system_clock > {},
                                    std::is_same< Clock, std::chrono::steady_clock > {} );
    }

private:
    // Specialization for system_clock: dispatch to detail::wait_handle_until
    template < class Duration >
    bool try_lock_until_impl( const std::chrono::time_point< std::chrono::system_clock, Duration >& abs_time,
                              std::true_type,   // is system_clock
                              std::false_type ) // is not steady_clock
    {
        while ( !try_lock() ) {
            if ( !detail::wait_handle_until( handle_, abs_time ) )
                return try_lock(); // one last attempt after timeout
        }
        return true;
    }

    // Specialization for steady_clock: dispatch to detail::wait_handle_until
    template < class Duration >
    bool try_lock_until_impl( const std::chrono::time_point< std::chrono::steady_clock, Duration >& abs_time,
                              std::false_type, // is not system_clock
                              std::true_type ) // is steady_clock
    {
        while ( !try_lock() ) {
            if ( !detail::wait_handle_until( handle_, abs_time ) )
                return try_lock(); // one last attempt after timeout
        }
        return true;
    }

    // Fallback for unknown clocks: compute remaining and delegate to try_lock_for
    template < class Clock, class Duration >
    bool try_lock_until_impl( const std::chrono::time_point< Clock, Duration >& abs_time,
                              std::false_type,  // is not system_clock
                              std::false_type ) // is not steady_clock
    {
        while ( true ) {
            auto rem = std::chrono::ceil< duration_type >( abs_time - Clock::now() );
            if ( rem.count() <= 0 )
                return try_lock();
            if ( try_lock_for( rem ) )
                return true;
            auto rem2 = std::chrono::ceil< duration_type >( abs_time - Clock::now() );
            if ( rem2.count() <= 0 )
                return try_lock();
        }
    }

public:
    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept NOVA_SYNC_RELEASE();

    /// @brief Returns the Semaphore HANDLE for async integration.
    ///
    /// When waiting on this handle, the semaphore will be signaled when the mutex
    /// is unlocked.  The caller can then attempt to acquire the lock (e.g. via
    /// `try_lock()`) and, if unsuccessful, re-register for notifications.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
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
    /// Call this after successfully acquiring the lock while registered as an async waiter.
    /// Safe to call when no notifications are pending.
    void consume_lock() const noexcept NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS;

private:
    HANDLE                  handle_ { nullptr };
    std::atomic< uint32_t > state_ { 0 }; // Bit 0: locked; Bits 1-31: waiter count
    HANDLE                  event_ { nullptr };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_EVENT_MUTEX
