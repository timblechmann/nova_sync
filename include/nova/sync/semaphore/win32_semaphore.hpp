// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( _WIN32 )
#  define NOVA_SYNC_HAS_WIN32_SEMAPHORE 1
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SEMAPHORE

#  include <chrono>
#  include <cstddef>
#  include <nova/sync/detail/timed_wait.hpp>

typedef void* HANDLE;

namespace nova::sync {

/// Counting semaphore backed by a Win32 semaphore kernel object. Windows only.
/// Exposes a waitable HANDLE via `native_handle()` for async integration.
class win32_semaphore
{
public:
    using native_handle_type = HANDLE;

    explicit win32_semaphore( std::ptrdiff_t initial = 0 );
    ~win32_semaphore();
    win32_semaphore( const win32_semaphore& )            = delete;
    win32_semaphore& operator=( const win32_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept;

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept;

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept;

    template < class Rep, class Period >
    bool try_acquire_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        using namespace std::chrono_literals;
        if ( rel_time <= 0ms )
            return try_acquire();
        return try_acquire_until( std::chrono::steady_clock::now() + rel_time );
    }

    template < class Clock, class Duration >
    bool try_acquire_until( const std::chrono::time_point< Clock, Duration >& abs_time )
    {
        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            if ( Clock::now() >= abs_time )
                return try_acquire();
            return detail::wait_handle_until( handle_, abs_time );
        } else {
            return try_acquire_for( abs_time - Clock::now() );
        }
    }

    /// Returns the underlying HANDLE (for async integration).
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return handle_;
    }

private:
    HANDLE handle_ { nullptr };
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_WIN32_SEMAPHORE
