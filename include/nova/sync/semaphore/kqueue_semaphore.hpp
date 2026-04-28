// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __APPLE__ )
#  define NOVA_SYNC_HAS_KQUEUE_SEMAPHORE 1
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_SEMAPHORE

#  include <atomic>
#  include <chrono>
#  include <cstddef>
#  include <nova/sync/detail/compat.hpp>
#  include <nova/sync/detail/timed_wait.hpp>

namespace nova::sync {

/// Counting semaphore backed by Apple `kqueue(EVFILT_USER)`. macOS/iOS only.
/// Exposes a pollable file descriptor via `native_handle()` for async integration.
class kqueue_semaphore
{
public:
    using native_handle_type = int;
    using duration_type      = std::chrono::nanoseconds;

    explicit kqueue_semaphore( std::ptrdiff_t initial = 0 );
    ~kqueue_semaphore();
    kqueue_semaphore( const kqueue_semaphore& )            = delete;
    kqueue_semaphore& operator=( const kqueue_semaphore& ) = delete;

    /// Adds @p n tokens and wakes up to @p n blocked waiters.
    void release( std::ptrdiff_t n = 1 ) noexcept;

    /// Blocks until a token is available, then consumes one.
    void acquire() noexcept;

    /// Consumes a token if available. Returns `true` on success, `false` if none available.
    [[nodiscard]] bool try_acquire() noexcept;

    bool try_acquire_for( duration_type rel_ns ) noexcept;

    template < class Rep, class Period >
    bool try_acquire_for( const std::chrono::duration< Rep, Period >& rel_time )
    {
        return try_acquire_for( std::chrono::duration_cast< duration_type >( rel_time ) );
    }

    template < class Clock, class Duration >
    bool try_acquire_until( const std::chrono::time_point< Clock, Duration >& abs_time )
    {
        if ( try_acquire() )
            return true;

        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            while ( true ) {
                if ( !detail::kevent_until( kqfd_, kqueue_ident_, abs_time ) )
                    return try_acquire();
                if ( try_acquire() )
                    return true;
            }
        } else {
            return try_acquire_for( abs_time - Clock::now() );
        }
    }

    /// Returns the underlying file descriptor (for async integration).
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
    }

private:
    static constexpr uintptr_t kqueue_ident_ = 10;

    const int              kqfd_;
    std::atomic< int32_t > count_;

    void notify() noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_SEMAPHORE
