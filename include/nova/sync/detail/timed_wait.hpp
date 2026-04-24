// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <cstdint>

namespace nova::sync::detail {

#ifdef __linux__

bool ppoll_for( int fd, std::chrono::nanoseconds ) noexcept;

bool ppoll_until( int lock_fd, const std::chrono::time_point< std::chrono::system_clock >& ) noexcept;
bool ppoll_until( int lock_fd, const std::chrono::time_point< std::chrono::steady_clock >& ) noexcept;

#elif defined( __APPLE__ )

bool kevent_for( int kqfd, std::chrono::nanoseconds rel_ns ) noexcept;

bool kevent_until( int kqfd, uintptr_t lock_ident, uintptr_t timer_ident ) noexcept;

bool kevent_until( int                                                         kqfd,
                   uintptr_t                                                   lock_ident,
                   const std::chrono::time_point< std::chrono::system_clock >& deadline ) noexcept;

bool kevent_until( int                                                         kqfd,
                   uintptr_t                                                   lock_ident,
                   const std::chrono::time_point< std::chrono::steady_clock >& deadline ) noexcept;

// ============================================================================
// Windows — WaitForSingleObject with relative millisecond timeout and retries
// ============================================================================
#elif defined( _WIN32 )

/// @brief Wait on a handle for up to the specified duration.
///
/// @return `true` if the handle was signaled, `false` on timeout or error.
bool wait_handle_for( HANDLE handle, std::chrono::milliseconds rel_ms ) noexcept;

/// @brief Wait on a lock handle until a timer signals or lock is signaled.
///
/// @return `true` if @p lock_handle is signaled, `false` if timer fires or on error.
bool wait_handle_until( HANDLE lock_handle, HANDLE timer_handle ) noexcept;

/// @brief Wait on a lock handle until an absolute deadline (system_clock).
///
/// @return `true` if signaled, `false` on timeout or error.
bool wait_handle_until( HANDLE                                                      lock_handle,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline ) noexcept;

/// @brief Wait on a lock handle until an absolute deadline (steady_clock).
///
/// @return `true` if signaled, `false` on timeout or error.
bool wait_handle_until( HANDLE                                                      lock_handle,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline ) noexcept;

#endif // platform

} // namespace nova::sync::detail
