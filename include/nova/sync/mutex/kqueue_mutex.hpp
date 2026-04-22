// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( __APPLE__ )
#    define NOVA_SYNC_HAS_KQUEUE_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX

#    include <atomic>
#    include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Simple async-capable mutex implemented via Apple `kqueue` with `EVFILT_USER`.
///
/// Uses the kqueue fd as a binary semaphore: `lock()` blocks in `kevent()` until
/// the fd becomes signalled, and `unlock()` posts `NOTE_TRIGGER`.  There is no
/// user-space waiter count — every `unlock()` always triggers the kqueue event.
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

    /// @brief Constructs an unlocked kqueue mutex.
    kqueue_mutex();
    ~kqueue_mutex();
    kqueue_mutex( const kqueue_mutex& )            = delete;
    kqueue_mutex& operator=( const kqueue_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept;

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    bool try_lock() noexcept;

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


/// @brief Async-capable mutex implemented via Apple `kqueue` with fast user-space path.
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
///   fast_kqueue_mutex mtx;
///   int fd = mtx.native_handle();
///
///   mtx.add_async_waiter();  // Register before waiting on fd
///   // Wait on fd for events (via kevent/libdispatch/Qt/Asio/etc.)
///   mtx.remove_async_waiter();  // Unregister when done
/// @endcode
///
class alignas( detail::hardware_destructive_interference_size ) fast_kqueue_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;

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

    /// @brief Returns the kqueue file descriptor for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
    }

    /// @brief Register an async waiter.
    ///
    /// Increments the waiter count so that `unlock()` triggers the kqueue event
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
    const int               kqfd_ { -1 };

    void lock_slow() noexcept;

    void consume_lock() const noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_MUTEX
