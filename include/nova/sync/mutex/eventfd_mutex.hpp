// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __linux__ )

#    define NOVA_SYNC_HAS_EVENTFD_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX

#    include <atomic>
#    include <nova/sync/detail/compat.hpp>

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
/// Async Waiter Integration
/// -------------------------
/// When integrating with event loop systems (Qt, Boost.Asio, etc.), call
/// `add_async_waiter()` before waiting on the `native_handle()` file descriptor,
/// and `remove_async_waiter()` after the wait completes or is cancelled.  This
/// ensures the fast-path optimization remains correct: if any async waiter is
/// pending, `unlock()` will always trigger the eventfd.
///
/// Example
/// -------
/// @code
///   fast_eventfd_mutex mtx;
///   int fd = mtx.native_handle();
///
///   mtx.add_async_waiter();  // Register before waiting on fd
///   // Wait on fd for readability (via select/poll/epoll/Qt/etc.)
///   mtx.remove_async_waiter();  // Unregister when done
/// @endcode
///
class alignas( detail::hardware_destructive_interference_size ) fast_eventfd_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;

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

private:
    std::atomic< uint32_t > state_ { 0 }; // Bit 0: locked; Bits 1-31: waiter count
    int                     evfd_ { -1 };

    void lock_slow() noexcept;

    void consume_lock() const noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
