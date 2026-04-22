// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#    define NOVA_SYNC_HAS_WIN32_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX

#    include <chrono>
#    include <nova/sync/detail/compat.hpp>

typedef void* HANDLE;

namespace nova::sync {

/// @brief Async-capable mutex implemented via Win32 `CreateMutex` kernel object.
///
class win32_mutex
{
public:
    /// @brief The native handle type — a Win32 HANDLE.
    using native_handle_type = HANDLE;

    /// @brief Constructs an unlocked win32_mutex.
    win32_mutex();
    ~win32_mutex();
    win32_mutex( const win32_mutex& )            = delete;
    win32_mutex& operator=( const win32_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept;

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    bool try_lock() noexcept;

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept;

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
