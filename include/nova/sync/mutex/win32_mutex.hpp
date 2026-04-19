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
class alignas( detail::hardware_destructive_interference_size ) win32_mutex
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
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
    }

    /// @brief RAII guard for a single async wait cycle.
    ///
    /// On Win32 the underlying kernel mutex handle is already waitable, so no
    /// waiter-count bookkeeping is needed. The guard simply keeps a pointer to
    /// the mutex and exposes its HANDLE for use with `WaitForMultipleObjects`,
    /// Boost.Asio `windows::object_handle`, or IOCP.
    ///
    /// A guard is **single-use**: once the event loop signals that the handle is
    /// signalled, destroy the guard and call `try_lock()`. If that fails, create
    /// a new guard with `make_async_wait_guard()` before re-registering.
    class async_wait_guard
    {
        win32_mutex* mtx_;

    public:
        explicit async_wait_guard( win32_mutex* m ) noexcept :
            mtx_( m )
        {}

        ~async_wait_guard() = default;

        async_wait_guard( async_wait_guard&& other ) noexcept :
            mtx_( other.mtx_ )
        {
            other.mtx_ = nullptr;
        }

        async_wait_guard& operator=( async_wait_guard&& other ) noexcept
        {
            if ( this != &other ) {
                mtx_       = other.mtx_;
                other.mtx_ = nullptr;
            }
            return *this;
        }

        async_wait_guard( const async_wait_guard& )            = delete;
        async_wait_guard& operator=( const async_wait_guard& ) = delete;

        /// @brief The Win32 HANDLE to register with your event loop.
        [[nodiscard]] native_handle_type native_handle() const noexcept
        {
            return mtx_ ? mtx_->native_handle() : nullptr;
        }
    };

    /// @brief Returns a single-use guard whose `native_handle()` can be passed
    ///        to `WaitForMultipleObjects` or an async framework. When the handle
    ///        is signalled, destroy the guard and call `try_lock()`.
    [[nodiscard]] async_wait_guard make_async_wait_guard() noexcept
    {
        return async_wait_guard( this );
    }

private:
    HANDLE handle_ { nullptr };

    bool try_lock_ms( unsigned long timeout_ms ) noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_MUTEX
