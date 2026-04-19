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

/// @brief Async-capable mutex implemented via Linux `eventfd`.
///
/// Exposes a file descriptor that becomes readable when the mutex is unlocked,
/// enabling integration with event loops.
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
    /// When waiting on this file descriptor, it becomes readable when the mutex is unlocked.
    /// The caller can then attempt to acquire the lock (e.g. via `try_lock()`) and, if unsuccessful, re-register for notifications.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return evfd_;
    }

private:
    const int evfd_ { -1 };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_EVENTFD_MUTEX
