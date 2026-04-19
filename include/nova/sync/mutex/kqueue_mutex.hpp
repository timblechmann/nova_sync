// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once


#if defined( __APPLE__ )
#    define NOVA_SYNC_HAS_KQUEUE_MUTEX 1
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX

#    include <atomic>
#    include <nova/sync/detail/compat.hpp>

namespace nova::sync {

/// @brief Async-capable mutex implemented via Apple `kqueue` with `EVFILT_USER`.
///
/// Exposes a file descriptor that becomes readable when the mutex is unlocked,
/// enabling integration with event loops.
///
class alignas( detail::hardware_destructive_interference_size ) kqueue_mutex
{
public:
    /// @brief The native handle type — a POSIX file descriptor.
    using native_handle_type = int;

    /// @brief Constructs an unlocked kqueue mutex.
    kqueue_mutex();
    ~kqueue_mutex();
    kqueue_mutex( const kqueue_mutex& )            = delete;
    kqueue_mutex& operator=( const kqueue_mutex& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() noexcept
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_weak( expected, 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow();
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    bool try_lock() noexcept
    {
        uint32_t s = state_.load( std::memory_order_relaxed );
        while ( ( s & 1u ) == 0 ) {
            if ( state_.compare_exchange_weak( s, s | 1u, std::memory_order_acquire, std::memory_order_relaxed ) )
                return true;
        }
        return false;
    }

    /// @brief Releases the lock and wakes one waiting thread if any.
    void unlock() noexcept;

    /// @brief Returns the kqueue file descriptor for async integration.
    [[nodiscard]] native_handle_type native_handle() const noexcept
    {
        return kqfd_;
    }

    /// @brief RAII guard for a single async wait cycle.
    ///
    /// Construction registers this caller as an async waiter (so `unlock()`
    /// will signal the kqueue fd even from the fast path). Destruction drains
    /// any pending notification and unregisters the waiter.
    ///
    /// A guard is **single-use**: once the event loop signals readability, call
    /// `try_lock()`. If that fails (spurious wakeup), discard this guard and
    /// call `make_async_wait_guard()` again before re-registering with the
    /// event loop.
    class async_wait_guard
    {
        kqueue_mutex* mtx_;

    public:
        explicit async_wait_guard( kqueue_mutex* m ) noexcept :
            mtx_( m )
        {
            if ( mtx_ )
                mtx_->add_async_waiter();
        }

        ~async_wait_guard()
        {
            if ( mtx_ ) {
                mtx_->consume_lock();
                mtx_->remove_async_waiter();
            }
        }

        async_wait_guard( async_wait_guard&& other ) noexcept :
            mtx_( other.mtx_ )
        {
            other.mtx_ = nullptr;
        }

        async_wait_guard& operator=( async_wait_guard&& other ) noexcept
        {
            if ( this != &other ) {
                if ( mtx_ ) {
                    mtx_->consume_lock();
                    mtx_->remove_async_waiter();
                }
                mtx_       = other.mtx_;
                other.mtx_ = nullptr;
            }
            return *this;
        }

        async_wait_guard( const async_wait_guard& )            = delete;
        async_wait_guard& operator=( const async_wait_guard& ) = delete;

        /// @brief The kqueue fd to register with your event loop for readability.
        [[nodiscard]] native_handle_type native_handle() const noexcept
        {
            return mtx_ ? mtx_->native_handle() : -1;
        }
    };

    /// @brief Returns a single-use guard that registers this thread as an async
    ///        waiter. Register the guard's `native_handle()` with your event loop
    ///        for readability; when it fires, destroy the guard and call
    ///        `try_lock()`.
    [[nodiscard]] async_wait_guard make_async_wait_guard() noexcept
    {
        return async_wait_guard( this );
    }

private:
    std::atomic< uint32_t > state_ { 0 }; // Bit 0: locked; Bits 1-31: waiter count
    const int               kqfd_ { -1 };

    void lock_slow() noexcept;

    void consume_lock() const noexcept;

    void add_async_waiter() noexcept
    {
        state_.fetch_add( 2u, std::memory_order_relaxed );
    }

    void remove_async_waiter() noexcept
    {
        state_.fetch_sub( 2u, std::memory_order_relaxed );
    }
};

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_MUTEX
