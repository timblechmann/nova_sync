// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/detail/pause.hpp>
#include <nova/sync/futex/atomic_wait.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

// =============================================================================
// Platform selection
// =============================================================================
#if defined( __linux__ )
#  include <cerrno>
#  include <climits>
#  include <linux/futex.h>
#  include <sys/syscall.h>
#  include <time.h>
#  include <unistd.h>

#  define NOVA_SYNC_FUTEX_LINUX 1

#elif defined( _WIN32 )
#  include <windows.h>

#  define NOVA_SYNC_FUTEX_WIN32 1

#else
// Portable fallback: hash-table of mutex + condvar buckets.
#  include <array>
#  include <condition_variable>
#  include <mutex>

#  define NOVA_SYNC_FUTEX_PORTABLE 1

#endif

using namespace std::chrono_literals;

namespace nova::sync {

// =============================================================================
// Linux — native futex
// =============================================================================
#if defined( NOVA_SYNC_FUTEX_LINUX )

namespace {

inline int
futex_syscall( std::atomic< int32_t >* addr, int op, int32_t val, const struct timespec* timeout, int32_t val3 ) noexcept
{
    return static_cast< int >(
        ::syscall( SYS_futex, reinterpret_cast< int32_t* >( addr ), op, val, timeout, nullptr, val3 ) );
}

template < clockid_t ClockId >
struct timespec clock_now_plus( std::chrono::nanoseconds ns ) noexcept
{
    struct timespec ts {};
    ::clock_gettime( ClockId, &ts );
    ts.tv_sec += time_t( ns.count() / 1'000'000'000LL );
    ts.tv_nsec += long( ns.count() % 1'000'000'000LL );
    if ( ts.tv_nsec >= 1'000'000'000L ) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1'000'000'000L;
    }
    return ts;
}

template < clockid_t ClockId >
struct timespec to_abs_timespec( std::chrono::nanoseconds ns_since_epoch ) noexcept
{
    return timespec {
        .tv_sec  = time_t( ns_since_epoch.count() / 1'000'000'000LL ),
        .tv_nsec = long( ns_since_epoch.count() % 1'000'000'000LL ),
    };
}

// Acquire fence must precede load for synchronization with notify's release fence.
// See [atomics.fences] p4/p8: fence(acquire) A synchronizes-with fence(release) B
// when load Y is sequenced after A and reads value stored before B.


inline bool acquire_and_check( std::atomic< int32_t >& atom, int32_t old, std::memory_order order ) noexcept
{
    if ( order != std::memory_order_relaxed )
        std::atomic_thread_fence( std::memory_order_acquire );
    return atom.load( std::memory_order_relaxed ) != old;
}

} // namespace

// void atomic_wait( std::atomic< int32_t >& atom, int32_t old, std::memory_order order ) noexcept
// {
//     {
//         auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire :
//         std::memory_order_relaxed; if ( atom.load( load_order ) != old )
//             return;
//     }

//     while ( true ) {
//         int rc = futex_syscall( &atom, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, old, nullptr, 0 );

//         if ( rc == 0 || ( rc < 0 && errno == EAGAIN ) ) {
//             if ( acquire_and_check( atom, old, order ) )
//                 return;
//             continue;
//         }
//         if ( rc < 0 && errno == EINTR )
//             continue;
//         return;
//     }
// }

void atomic_wait( std::atomic< int32_t >& atom, int32_t old, std::memory_order order ) noexcept
{
    auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;

    // Phase 1: CPU Yielding (Active Spinning with Exponential Backoff)
    // Avoids the syscall completely if the atomic changes within microseconds.
    int           pause_count      = 1;
    constexpr int max_active_spins = 10;

    for ( int i = 0; i < max_active_spins; ++i ) {
        if ( atom.load( load_order ) != old )
            return;

        for ( int j = 0; j < pause_count; ++j )
            detail::pause();

        pause_count *= 2; // Exponential backoff
    }

    // Phase 2: OS Yielding (Passive Spinning)
    // The lock is held slightly longer than a few cycles. Surrender our
    // timeslice so the thread holding the lock has CPU time to release it.
    constexpr int max_yield_spins = 4;
    for ( int i = 0; i < max_yield_spins; ++i ) {
        if ( atom.load( load_order ) != old )
            return;

        std::this_thread::yield();
    }

    // Phase 3: Slow Path (OS-Level Blocking Wait)
    // The condition is taking a long time; fall back to the kernel safely.
    while ( true ) {
        int rc = futex_syscall( &atom, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, old, nullptr, 0 );

        if ( rc == 0 || ( rc < 0 && errno == EAGAIN ) ) {
            // EAGAIN means the futex value changed just as we entered the kernel.
            if ( acquire_and_check( atom, old, order ) )
                return;
            continue;
        }
        if ( rc < 0 && errno == EINTR )
            continue; // Interrupted by a signal, try again
        return;
    }
}

bool atomic_wait_for( std::atomic< int32_t >&  atom,
                      int32_t                  old,
                      std::chrono::nanoseconds rel,
                      std::memory_order        order ) noexcept
{
    if ( rel <= 0ns )
        return atom.load( std::memory_order_relaxed ) != old;

    struct timespec abs_ts = clock_now_plus< CLOCK_MONOTONIC >( rel );

    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    while ( true ) {
        int rc = futex_syscall( &atom, FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, old, &abs_ts, FUTEX_BITSET_MATCH_ANY );

        if ( rc == 0 || ( rc < 0 && errno == EAGAIN ) ) {
            if ( acquire_and_check( atom, old, order ) )
                return true;
            continue;
        }
        if ( rc < 0 && errno == ETIMEDOUT )
            return atom.load( std::memory_order_relaxed ) != old;
        if ( rc < 0 && errno == EINTR )
            continue;
        return false;
    }
}

bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline,
                        std::memory_order                                           order ) noexcept
{
    auto            ns     = std::chrono::duration_cast< std::chrono::nanoseconds >( deadline.time_since_epoch() );
    struct timespec abs_ts = to_abs_timespec< CLOCK_MONOTONIC >( ns );

    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    while ( true ) {
        int rc = futex_syscall( &atom, FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG, old, &abs_ts, FUTEX_BITSET_MATCH_ANY );

        if ( rc == 0 || ( rc < 0 && errno == EAGAIN ) ) {
            if ( acquire_and_check( atom, old, order ) )
                return true;
            continue;
        }
        if ( rc < 0 && errno == ETIMEDOUT )
            return atom.load( std::memory_order_relaxed ) != old;
        if ( rc < 0 && errno == EINTR )
            continue;
        return false;
    }
}

bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline,
                        std::memory_order                                           order ) noexcept
{
    auto            ns     = std::chrono::duration_cast< std::chrono::nanoseconds >( deadline.time_since_epoch() );
    struct timespec abs_ts = to_abs_timespec< CLOCK_REALTIME >( ns );

    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    while ( true ) {
        int rc = futex_syscall(
            &atom, FUTEX_WAIT_BITSET | FUTEX_CLOCK_REALTIME | FUTEX_PRIVATE_FLAG, old, &abs_ts, FUTEX_BITSET_MATCH_ANY );

        if ( rc == 0 || ( rc < 0 && errno == EAGAIN ) ) {
            if ( acquire_and_check( atom, old, order ) )
                return true;
            continue;
        }
        if ( rc < 0 && errno == ETIMEDOUT )
            return atom.load( std::memory_order_relaxed ) != old;
        if ( rc < 0 && errno == EINTR )
            continue;
        return false;
    }
}

void atomic_notify_one( std::atomic< int32_t >& atom ) noexcept
{
    futex_syscall( &atom, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, nullptr, 0 );
}

void atomic_notify_all( std::atomic< int32_t >& atom ) noexcept
{
    futex_syscall( &atom, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX, nullptr, 0 );
}

// =============================================================================
// Windows — WaitOnAddress
// =============================================================================
#elif defined( NOVA_SYNC_FUTEX_WIN32 )

void atomic_wait( std::atomic< int32_t >& atom, int32_t old, std::memory_order order ) noexcept
{
    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return;
    }

    while ( true ) {
        BOOL ok = ::WaitOnAddress( reinterpret_cast< volatile void* >( &atom ), &old, sizeof( int32_t ), INFINITE );

        if ( order != std::memory_order_relaxed )
            std::atomic_thread_fence( std::memory_order_acquire );
        if ( atom.load( std::memory_order_relaxed ) != old )
            return;

        (void)ok;
    }
}

bool atomic_wait_for( std::atomic< int32_t >&  atom,
                      int32_t                  old,
                      std::chrono::nanoseconds rel,
                      std::memory_order        order ) noexcept
{
    if ( rel <= 0ns )
        return atom.load( std::memory_order_relaxed ) != old;

    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    auto ms = std::chrono::duration_cast< std::chrono::milliseconds >( rel );
    if ( ms.count() == 0 && rel.count() > 0 )
        ms = std::chrono::milliseconds( 1 );

    DWORD timeout_ms = static_cast< DWORD >( std::min< int64_t >( ms.count(), INFINITE - 1 ) );

    BOOL ok = ::WaitOnAddress( reinterpret_cast< volatile void* >( &atom ), &old, sizeof( int32_t ), timeout_ms );

    if ( order != std::memory_order_relaxed )
        std::atomic_thread_fence( std::memory_order_acquire );

    (void)ok;
    return atom.load( std::memory_order_relaxed ) != old;
}

bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline,
                        std::memory_order                                           order ) noexcept
{
    while ( true ) {
        auto remaining = deadline - std::chrono::steady_clock::now();
        if ( remaining <= 0ns )
            return atom.load( std::memory_order_relaxed ) != old;

        if ( atomic_wait_for( atom, old, remaining, order ) )
            return true;

        if ( atom.load( std::memory_order_relaxed ) != old )
            return true;

        if ( std::chrono::steady_clock::now() >= deadline )
            return false;
    }
}

bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline,
                        std::memory_order                                           order ) noexcept
{
    auto remaining = deadline - std::chrono::system_clock::now();
    return atomic_wait_for( atom, old, std::chrono::duration_cast< std::chrono::nanoseconds >( remaining ), order );
}

void atomic_notify_one( std::atomic< int32_t >& atom ) noexcept
{
    ::WakeByAddressSingle( reinterpret_cast< void* >( std::addressof( atom ) ) );
}

void atomic_notify_all( std::atomic< int32_t >& atom ) noexcept
{
    ::WakeByAddressAll( reinterpret_cast< void* >( std::addressof( atom ) ) );
}

// =============================================================================
// Portable fallback — hash table of mutex + condvar buckets
// =============================================================================
#elif defined( NOVA_SYNC_FUTEX_PORTABLE )

namespace {

struct wait_bucket
{
    std::mutex              mutex;
    std::condition_variable cv;
};

constexpr std::size_t bucket_count = 67;

wait_bucket& bucket_for( const void* addr ) noexcept
{
    static std::array< wait_bucket, bucket_count > buckets;
    auto                                           h = reinterpret_cast< std::uintptr_t >( addr );
    h ^= h >> 16;
    h *= 0x9e3779b9u;
    return buckets[ h % bucket_count ];
}

} // namespace

void atomic_wait( std::atomic< int32_t >& atom, int32_t old, std::memory_order order ) noexcept
{
    atom.wait( old, order );
}

bool atomic_wait_for( std::atomic< int32_t >&  atom,
                      int32_t                  old,
                      std::chrono::nanoseconds rel,
                      std::memory_order        order ) noexcept
{
    if ( rel <= 0ns )
        return atom.load( std::memory_order_relaxed ) != old;

    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    auto&            b = bucket_for( &atom );
    std::unique_lock lock( b.mutex );

    if ( atom.load( std::memory_order_relaxed ) != old )
        return true;

    b.cv.wait_for( lock, rel, [ & ] {
        return atom.load( std::memory_order_relaxed ) != old;
    } );

    if ( order != std::memory_order_relaxed )
        std::atomic_thread_fence( std::memory_order_acquire );
    return atom.load( std::memory_order_relaxed ) != old;
}

bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline,
                        std::memory_order                                           order ) noexcept
{
    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    auto&            b = bucket_for( &atom );
    std::unique_lock lock( b.mutex );

    if ( atom.load( std::memory_order_relaxed ) != old )
        return true;

    b.cv.wait_until( lock, deadline, [ & ] {
        return atom.load( std::memory_order_relaxed ) != old;
    } );

    if ( order != std::memory_order_relaxed )
        std::atomic_thread_fence( std::memory_order_acquire );
    return atom.load( std::memory_order_relaxed ) != old;
}

bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline,
                        std::memory_order                                           order ) noexcept
{
    {
        auto load_order = ( order != std::memory_order_relaxed ) ? std::memory_order_acquire : std::memory_order_relaxed;
        if ( atom.load( load_order ) != old )
            return true;
    }

    auto&            b = bucket_for( &atom );
    std::unique_lock lock( b.mutex );

    if ( atom.load( std::memory_order_relaxed ) != old )
        return true;

    b.cv.wait_until( lock, deadline, [ & ] {
        return atom.load( std::memory_order_relaxed ) != old;
    } );

    if ( order != std::memory_order_relaxed )
        std::atomic_thread_fence( std::memory_order_acquire );
    return atom.load( std::memory_order_relaxed ) != old;
}

void atomic_notify_one( std::atomic< int32_t >& atom ) noexcept
{
    atom.notify_one();
    auto&           b = bucket_for( &atom );
    std::lock_guard lock( b.mutex );
    b.cv.notify_one();
}

void atomic_notify_all( std::atomic< int32_t >& atom ) noexcept
{
    atom.notify_all();
    auto&           b = bucket_for( &atom );
    std::lock_guard lock( b.mutex );
    b.cv.notify_all();
}

#endif // platform

} // namespace nova::sync
