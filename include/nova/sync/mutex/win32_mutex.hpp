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

/// @brief Non-recursive mutex using Win32 `CreateMutex` kernel object.
///
/// The underlying Win32 mutex handle can be duplicated and integrated into
/// event loops via `WaitForMultipleObjects`, Boost.Asio, or IOCP.
///
class win32_mutex
{
public:
    using native_handle_type = HANDLE;

    /// @brief Constructs an unlocked win32 mutex.
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

    /// @brief Attempts to acquire within a timeout duration.
    template < typename Rep, typename Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& timeout ) noexcept
    {
        auto ms = std::chrono::duration_cast< std::chrono::milliseconds >( timeout );
        return try_lock_until( std::chrono::steady_clock::now() + ms );
    }

    /// @brief Attempts to acquire until an absolute time point.
    template < typename Clock, typename Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& deadline ) noexcept
    {
        auto now          = Clock::now();
        auto remaining    = deadline - now;
        auto remaining_ms = std::chrono::duration_cast< std::chrono::milliseconds >( remaining );
        if ( remaining_ms.count() < 0 )
            return try_lock();
        return try_lock_for( remaining_ms );
    }

    /// @brief Returns the Win32 HANDLE for async integration.
    /// when waiting on this handle, the mutex will be signaled when it becomes available. When
    /// this happens, the mutex will be owned by the calling thread.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
    }


private:
    HANDLE handle_ { nullptr };

    bool try_lock_ms( unsigned long timeout_ms ) noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_MUTEX
