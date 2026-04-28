// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( __APPLE__ )
#  define NOVA_SYNC_HAS_KQUEUE_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX

#  include <atomic>
#  include <chrono>
#  include <nova/sync/detail/compat.hpp>
#  include <nova/sync/detail/timed_wait.hpp>
#  include <nova/sync/mutex/support/async_waiter_guard.hpp>
#  include <nova/sync/mutex/tsa_annotations.hpp>

namespace nova::sync {

/// @brief Simple async-capable mutex implemented via Apple `kqueue` with `EVFILT_USER`.
///
class NOVA_SYNC_CAPABILITY( "mutex" ) kqueue_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;
    /// @brief Effective timeout resolution (kevent timespec is nanosecond-precise).
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked kqueue mutex.
    kqueue_mutex();
    ~kqueue_mutex();
    kqueue_mutex( const kqueue_mutex& )            = delete;
    kqueue_mutex& operator=( const kqueue_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept NOVA_SYNC_ACQUIRE();

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true );

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_ns nanoseconds.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    bool try_lock_for( duration_type rel_ns ) noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( rel_ns.count() <= 0 )
            return try_lock();
        while ( !try_lock() ) {
            if ( !detail::kevent_for( kqfd_, rel_ns ) )
                return try_lock(); // one last attempt after timeout
            return true;
        }
        return true;
    }

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_for( std::chrono::duration_cast< duration_type >( rel_time ) );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( try_lock() )
            return true;

        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            while ( true ) {
                if ( !detail::kevent_until( kqfd_, 1, abs_time ) )
                    return try_lock(); // one last attempt after timeout
                return true;
            }
        } else {
            return try_lock_until_fallback( abs_time - Clock::now() );
        }
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept NOVA_SYNC_RELEASE();

    /// @brief Returns the kqueue file descriptor for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
    }

private:
    const int kqfd_;
};


/// @brief Fast async-capable mutex with user-space fast path and kqueue kernel fallback.
///
class alignas( detail::hardware_destructive_interference_size ) NOVA_SYNC_CAPABILITY( "mutex" ) fast_kqueue_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;
    /// @brief Effective timeout resolution (kevent timespec is nanosecond-precise).
    using duration_type      = std::chrono::nanoseconds;

    /// @brief Constructs an unlocked fast_kqueue_mutex.
    fast_kqueue_mutex();
    ~fast_kqueue_mutex();
    fast_kqueue_mutex( const fast_kqueue_mutex& )            = delete;
    fast_kqueue_mutex& operator=( const fast_kqueue_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow();
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( ( s & 1u ) == 0 ) {
            if ( state_.compare_exchange_weak( s, s | 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept NOVA_SYNC_RELEASE();

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    ///
    /// @return `true` if the lock was acquired, `false` if the duration expired.
    template < class Rep, class Period >
    bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return try_lock_for_impl( rel_time );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time is reached.
    ///
    /// @return `true` if the lock was acquired, `false` if the deadline expired.
    template < class Clock, class Duration >
    bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time ) NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( try_lock() )
            return true;

        if constexpr ( std::is_same_v< Clock, std::chrono::system_clock >
                       || std::is_same_v< Clock, std::chrono::steady_clock > ) {
            // Register as async waiter so unlock() will trigger the kevent.
            auto                                            s = add_async_waiter();
            detail::async_waiter_guard< fast_kqueue_mutex > guard( *this, detail::adopt_async_waiter );

            while ( true ) {
                if ( ( s & 1u ) == 0 ) {
                    uint32_t desired = ( s - 2u ) | 1u;
                    if ( state_.compare_exchange_weak(
                             s, desired, std::memory_order_acquire, std::memory_order_relaxed ) ) {
                        // CAS succeeded: consume any pending NOTE_TRIGGER.
                        consume_lock();
                        guard.dismiss(); // waiter count already decremented in CAS
                        return true;
                    }
                    continue;
                }

                if ( !detail::kevent_until( kqfd_, 1, abs_time ) )
                    // Timed out — guard destructor calls remove_async_waiter().
                    return try_lock(); // one last attempt after timeout

                consume_lock();
                s = state_.load( std::memory_order_acquire );
            }
        } else {
            return try_lock_for( abs_time - Clock::now() );
        }
    }

    /// @brief Returns the kqueue file descriptor for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
    }

    /// @brief Register an async waiter before polling the file descriptor.
    uint32_t add_async_waiter() noexcept
    {
        return state_.fetch_add( 2u, std::memory_order_relaxed ) + 2u;
    }

    /// @brief Unregister a previously added async waiter.
    void remove_async_waiter() noexcept
    {
        state_.fetch_sub( 2u, std::memory_order_relaxed );
    }

    /// @brief Drain pending kernel notifications after acquiring the lock.
    void consume_lock() const noexcept;

private:
    std::atomic< uint32_t > state_ { 0 };
    const int               kqfd_ { -1 };

    void lock_slow() noexcept;

    bool try_lock_for_impl( std::chrono::nanoseconds ) noexcept;
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_MUTEX
