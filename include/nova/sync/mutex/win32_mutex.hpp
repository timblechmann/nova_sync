// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )

#  define NOVA_SYNC_HAS_WIN32_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX

#  include <chrono>
#  include <nova/sync/detail/timed_wait.hpp>
#  include <nova/sync/thread_safety/annotations.hpp>

typedef void* HANDLE;

namespace nova::sync {

/// @brief Async-capable mutex implemented via a Win32 semaphore kernel object.
///
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
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            if ( Clock::now() >= abs_time )
                return false;
            if ( detail::wait_handle_until( handle_, abs_time ) )
                return true;
            return false;
        } else {
            return try_lock_for( abs_time - Clock::now() );
        }
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept NOVA_SYNC_RELEASE();

    /// @brief Returns the HANDLE for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
    }

private:
    HANDLE handle_ { nullptr };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_MUTEX
