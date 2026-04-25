// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/detail/native_handle_support.hpp>
#include <nova/sync/detail/timed_wait.hpp>
#include <nova/sync/mutex/detail/async_support.hpp>

#include <optional>

#if defined( __linux__ )
#    include <cerrno>
#    include <poll.h>
#    include <sys/timerfd.h>
#    include <time.h>
#    include <unistd.h>
#elif defined( __APPLE__ )
#    include <sys/event.h>
#    include <sys/time.h>
#    include <sys/types.h>
#elif defined( _WIN32 )
#    include <windows.h>
#endif

namespace nova::sync::detail {

#if defined( __linux__ ) || defined( __APPLE__ )
struct timespec as_timespec( std::chrono::nanoseconds ns ) noexcept
{
    return timespec {
        .tv_sec  = time_t( ns.count() / 1'000'000'000LL ),
        .tv_nsec = long( ns.count() % 1'000'000'000LL ),
    };
}
#endif

using namespace std::chrono_literals;

// ============================================================================
// Linux — ppoll with EINTR handling and absolute timeout via timerfd
// ============================================================================
#if defined( __linux__ )

bool ppoll_for( int fd, std::chrono::nanoseconds rel ) noexcept
{
    using clock = std::chrono::steady_clock;

    auto                             start = clock::now();
    std::optional< clock::duration > elapsed;

    while ( true ) {
        struct pollfd pfd { fd, POLLIN, 0 };
        if ( elapsed )
            elapsed = clock::now() - start;
        else
            elapsed = 0ms;
        auto remaining = rel - *elapsed;
        if ( remaining <= 0ns )
            return false; // timeout

        struct timespec ts = as_timespec( remaining );

        int rc = ::ppoll( &pfd, 1, &ts, nullptr );
        if ( rc > 0 )
            return true;  // readable
        if ( rc == 0 )
            return false; // timeout
        // rc < 0: error
        if ( errno != EINTR )
            return false; // real error
        // EINTR: loop and recompute timeout
    }
}

static bool ppoll_until( int lock_fd, int timer_fd ) noexcept
{
    while ( true ) {
        struct pollfd fds[ 2 ] {
            { lock_fd, POLLIN, 0 },
            { timer_fd, POLLIN, 0 },
        };

        int rc = ::ppoll( fds, 2, nullptr, nullptr );
        if ( rc < 0 ) {
            if ( errno == EINTR )
                continue; // retry on signal
            return false;
        }
        if ( rc == 0 )
            continue; // spurious wakeup
        // Check which fd fired
        if ( fds[ 0 ].revents & POLLIN )
            return true;  // lock fd is readable
        if ( fds[ 1 ].revents & POLLIN )
            return false; // timer fired (timeout)
        // Other events (HUP, ERR): treat as timeout
        return false;
    }
}

/// @brief ppoll_until overload for system_clock deadlines.
///
/// Creates a timerfd internally, sets it to the deadline, and waits
/// on both the lock fd and the timer fd simultaneously.
bool ppoll_until( int lock_fd, const std::chrono::time_point< std::chrono::system_clock >& deadline ) noexcept
{
    scoped_file_descriptor timer_fd( ::timerfd_create( CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK ) );
    if ( !timer_fd )
        return false;

    auto remaining = deadline - std::chrono::system_clock::now();

    if ( remaining <= 0ns )
        return false;

    struct itimerspec spec {
        .it_interval = { 0, 0 },
        .it_value    = as_timespec( remaining ),
    };

    if ( ::timerfd_settime( timer_fd.get(), 0, &spec, nullptr ) < 0 )
        return false;

    return ppoll_until( lock_fd, timer_fd.get() );
}

/// @brief ppoll_until overload for steady_clock deadlines.
///
/// Creates a timerfd internally, sets it to the deadline, and waits
/// on both the lock fd and the timer fd simultaneously.
bool ppoll_until( int lock_fd, const std::chrono::time_point< std::chrono::steady_clock >& deadline ) noexcept
{
    scoped_file_descriptor timer_fd( ::timerfd_create( CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK ) );
    if ( !timer_fd )
        return false;

    auto remaining = deadline - std::chrono::steady_clock::now();

    if ( remaining <= 0ns )
        return false;

    struct itimerspec spec {
        .it_interval = { 0, 0 },
        .it_value    = as_timespec( remaining ),
    };

    if ( ::timerfd_settime( timer_fd.get(), 0, &spec, nullptr ) < 0 )
        return false;

    return ppoll_until( lock_fd, timer_fd.get() );
}

// ============================================================================
// macOS — kevent with EINTR handling and dual-event timeout
// ============================================================================
#elif defined( __APPLE__ )

bool kevent_for( int kqfd, std::chrono::nanoseconds rel_ns ) noexcept
{
    using clock = std::chrono::steady_clock;

    auto                             start = clock::now();
    std::optional< clock::duration > elapsed;

    while ( true ) {
        struct kevent out {};
        if ( elapsed )
            elapsed = clock::now() - start;
        else
            elapsed = 0ns;
        auto remaining_ns = rel_ns - *elapsed;

        if ( remaining_ns <= 0ns )
            return false; // timeout

        struct timespec ts = as_timespec( remaining_ns );

        int rc = ::kevent( kqfd, nullptr, 0, &out, 1, &ts );
        if ( rc > 0 )
            return true;  // event fired
        if ( rc == 0 )
            return false; // timeout
        // rc < 0: error
        if ( errno != EINTR )
            return false; // real error
        // EINTR: loop and recompute timeout
    }
}

/// @brief Wait on two kqueue identifiers: the lock event and a timer event.
///
/// Returns true if the lock event fires, false if the timer fires first.
bool kevent_until( int kqfd, uintptr_t lock_ident, uintptr_t timer_ident ) noexcept
{
    struct kevent out {};

    while ( true ) {
        int rc = ::kevent( kqfd, nullptr, 0, &out, 1, nullptr );
        if ( rc < 0 ) {
            if ( errno == EINTR )
                continue; // retry on signal
            return false;
        }
        if ( rc == 0 )
            continue; // spurious wakeup (should not happen with infinite timeout)
        // Check which event fired
        if ( out.ident == lock_ident )
            return true;  // lock event
        if ( out.ident == timer_ident )
            return false; // timer (timeout)
        // Unknown event: treat as timeout
        return false;
    }
}

bool kevent_until( int                                                         kqfd,
                   uintptr_t                                                   lock_ident,
                   const std::chrono::time_point< std::chrono::system_clock >& deadline ) noexcept
{
    constexpr uintptr_t timer_ident = 0xdeadbeefULL;

    // Convert deadline to nanoseconds since Unix epoch
    auto deadline_ns = std::chrono::nanoseconds( deadline.time_since_epoch() );

    struct kevent kev {};
    EV_SET( &kev,
            timer_ident,
            EVFILT_TIMER,
            EV_ADD | EV_ONESHOT,
            NOTE_ABSOLUTE | NOTE_NSECONDS,
            deadline_ns.count(),
            nullptr );

    if ( ::kevent( kqfd, &kev, 1, nullptr, 0, nullptr ) < 0 )
        return false;

    bool result = kevent_until( kqfd, lock_ident, timer_ident );

    // Clean up timer event if it hasn't fired yet
    EV_SET( &kev, timer_ident, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr );
    ::kevent( kqfd, &kev, 1, nullptr, 0, nullptr );

    return result;
}

bool kevent_until( int kqfd,
                   uintptr_t /*lock_ident*/,
                   const std::chrono::time_point< std::chrono::steady_clock >& deadline ) noexcept
{
    return kevent_for( kqfd, deadline - std::chrono::steady_clock::now() );
}

// ============================================================================
// Windows — WaitForMultipleObjects with two handles (lock + timer)
// ============================================================================
#elif defined( _WIN32 )

bool wait_handle_for( HANDLE handle, std::chrono::nanoseconds rel ) noexcept
{
    if ( rel <= 0ns )
        return false;

    auto timer_handle = scoped_handle( ::CreateWaitableTimerW( nullptr, TRUE, nullptr ) );
    if ( !timer_handle )
        return false;

    // Windows FILETIME units are 100-nanosecond intervals.
    // Negative value means relative time from now.
    LONGLONG filetime_units = -( rel.count() / 100 );
    if ( filetime_units == 0 )
        filetime_units = -1; // ensure at least one tick of wait

    LARGE_INTEGER due_time;
    due_time.QuadPart = filetime_units;

    if ( !::SetWaitableTimer( timer_handle.get(), &due_time, 0, nullptr, nullptr, FALSE ) )
        return false;

    return wait_handle_until( handle, timer_handle.get() );
}

/// @brief Wait on two handles: the lock handle and a timer handle.
///
/// Returns true if the lock handle is signaled, false if the timer fires first.
bool wait_handle_until( HANDLE lock_handle, HANDLE timer_handle ) noexcept
{
    HANDLE handles[ 2 ] { lock_handle, timer_handle };

    while ( true ) {
        DWORD rc = ::WaitForMultipleObjects( 2, handles, FALSE, INFINITE );

        if ( rc == WAIT_OBJECT_0 )
            return true;  // lock handle signaled
        if ( rc == WAIT_OBJECT_0 + 1 )
            return false; // timer handle signaled (timeout)
        // WAIT_FAILED or other: treat as timeout
        return false;
    }
}

/// @brief wait_handle_until overload for system_clock deadlines.
///
/// Creates a waitable timer internally, sets it to the deadline, and waits
/// on both the lock handle and the timer handle simultaneously.
bool wait_handle_until( HANDLE                                                      lock_handle,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline ) noexcept
{
    auto timer_handle = scoped_handle( ::CreateWaitableTimerW( nullptr, TRUE, nullptr ) );
    if ( !timer_handle )
        return false;

    auto remaining = deadline - std::chrono::system_clock::now();

    if ( remaining <= 0ms )
        return false;

    // Convert to 100-nanosecond intervals (Windows FILETIME unit)
    // Negative values mean relative time from now
    LONGLONG filetime_units = -( std::chrono::nanoseconds( remaining ).count() / 100 );

    LARGE_INTEGER due_time;
    due_time.QuadPart = filetime_units;

    if ( !::SetWaitableTimer( timer_handle.get(), &due_time, 0, nullptr, nullptr, FALSE ) )
        return false;

    return wait_handle_until( lock_handle, timer_handle.get() );
}

/// @brief wait_handle_until overload for steady_clock deadlines.
///
/// Creates a waitable timer internally, sets it to the deadline, and waits
/// on both the lock handle and the timer handle simultaneously.
///
/// Note: steady_clock deadlines are converted relative to now, as Windows
/// waitable timers use system time, not monotonic time.
bool wait_handle_until( HANDLE                                                      lock_handle,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline ) noexcept
{
    auto timer_handle = scoped_handle( ::CreateWaitableTimerW( nullptr, TRUE, nullptr ) );
    if ( !timer_handle )
        return false;

    auto remaining = deadline - std::chrono::steady_clock::now();

    if ( remaining <= 0ms )
        return false;

    // Convert to 100-nanosecond intervals (Windows FILETIME unit)
    // Negative values mean relative time from now
    LONGLONG filetime_units = -( std::chrono::nanoseconds( remaining ).count() / 100 );

    LARGE_INTEGER due_time;
    due_time.QuadPart = filetime_units;

    if ( !::SetWaitableTimer( timer_handle.get(), &due_time, 0, nullptr, nullptr, FALSE ) )
        return false;

    return wait_handle_until( lock_handle, timer_handle.get() );
}

#endif // platform

} // namespace nova::sync::detail
