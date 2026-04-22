// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#    define NOVA_SYNC_HAS_WIN32_EVENT_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_EVENT_MUTEX

#    include <atomic>
#    include <nova/sync/detail/compat.hpp>

typedef void* HANDLE;

namespace nova::sync {

/// @brief Async-capable, non-recursive mutex using Win32 Semaphore and auto-reset Event.
///
class win32_event_mutex
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
    void lock() noexcept;

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    bool try_lock() noexcept;

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept;

    /// @brief Returns the Semaphore HANDLE for async integration.
    ///
    /// When waiting on this handle, the semaphore will be signaled when the mutex
    /// is unlocked.  The caller can then attempt to acquire the lock (e.g. via
    /// `try_lock()`) and, if unsuccessful, re-register for notifications.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
    }

    void add_async_waiter() noexcept
    {
        state_.fetch_add( 2u, std::memory_order_relaxed );
    }

    void remove_async_waiter() noexcept
    {
        state_.fetch_sub( 2u, std::memory_order_relaxed );
    }

private:
    HANDLE                  handle_ { nullptr };
    std::atomic< uint32_t > state_ { 0 }; // Bit 0: locked; Bits 1-31: waiter count
    HANDLE                  event_ { nullptr };

    void consume_lock() const noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_EVENT_MUTEX
