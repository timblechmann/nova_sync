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

/// @brief Single WaitForSingleObject call with relative millisecond timeout and retries.
///
/// Automatically recomputes timeout on timeout and retries.  May make now() calls internally.
/// The caller is responsible for ceiling-rounding to milliseconds before calling.
///
/// @param handle  Win32 HANDLE to wait on.
/// @param rel_ms  Relative timeout in milliseconds.  Must be > 0.
/// @return `true` if the handle was signaled, `false` on timeout or error.
bool wait_handle_for( HANDLE handle, std::chrono::milliseconds rel_ms ) noexcept;

/// @brief Wait on two handles: the lock handle and a timer handle.
///
/// No calls to now() needed; the timer handle manages the deadline.
///
/// @param lock_handle   The lock handle to wait on.
/// @param timer_handle  The timer handle (set to absolute deadline).
/// @return `true` if @p lock_handle is signaled, `false` if timer fires (timeout).
bool wait_handle_until( HANDLE lock_handle, HANDLE timer_handle ) noexcept;

/// @brief Wait on a lock handle until an absolute deadline (system_clock specialization).
///
/// Creates a waitable timer internally, sets it to the deadline, and waits
/// on both the lock handle and the timer handle simultaneously using
/// WaitForMultipleObjects.  The timer is automatically destroyed when the
/// function returns.
///
/// @param lock_handle  The lock handle to wait on.
/// @param deadline     Absolute time_point on system_clock to wait until.
/// @return `true` if @p lock_handle is signaled, `false` if deadline expires.
bool wait_handle_until( HANDLE                                                      lock_handle,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline ) noexcept;

/// @brief Wait on a lock handle until an absolute deadline (steady_clock specialization).
///
/// Creates a waitable timer internally, sets it to the deadline, and waits
/// on both the lock handle and the timer handle simultaneously using
/// WaitForMultipleObjects.  The timer is automatically destroyed when the
/// function returns.
///
/// @param lock_handle  The lock handle to wait on.
/// @param deadline     Absolute time_point on steady_clock to wait until.
/// @return `true` if @p lock_handle is signaled, `false` if deadline expires.
bool wait_handle_until( HANDLE                                                      lock_handle,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline ) noexcept;

#endif // platform

} // namespace nova::sync::detail
