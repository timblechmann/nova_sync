// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <thread>

#include <nova/sync/detail/compat.hpp>
#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/mutex/policies.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

namespace nova::sync {

//----------------------------------------------------------------------------------------------------------------------
// Concrete implementation classes (slow paths compiled into a .cpp TU)

namespace impl {

/// @brief Plain spinlock (CPU-pause hints only).
class NOVA_SYNC_CAPABILITY( "mutex" ) spinlock_plain
{
public:
    spinlock_plain()                                   = default;
    spinlock_plain( const spinlock_plain& )            = delete;
    spinlock_plain& operator=( const spinlock_plain& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;
        lock_slow();
    }

    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        if ( locked_.load( std::memory_order_relaxed ) )
            return false;
        return !locked_.exchange( true, std::memory_order_acquire );
    }

    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        locked_.store( false, std::memory_order_release );
    }

protected:
    std::atomic< bool > locked_ { false };

private:
    void lock_slow() noexcept;
};

/// @brief Spinlock with exponential backoff.
class NOVA_SYNC_CAPABILITY( "mutex" ) spinlock_backoff : protected spinlock_plain
{
public:
    spinlock_backoff()                                     = default;
    spinlock_backoff( const spinlock_backoff& )            = delete;
    spinlock_backoff& operator=( const spinlock_backoff& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        if ( !locked_.exchange( true, std::memory_order_acquire ) )
            return;
        lock_slow();
    }

    using spinlock_plain::try_lock;
    using spinlock_plain::unlock;

private:
    void lock_slow() noexcept;
};

/// @brief Recursive spinlock (CPU-pause hints only).
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY recursive_spinlock_plain
{
public:
    recursive_spinlock_plain()                                             = default;
    recursive_spinlock_plain( const recursive_spinlock_plain& )            = delete;
    recursive_spinlock_plain& operator=( const recursive_spinlock_plain& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        const std::thread::id tid = std::this_thread::get_id();
        std::thread::id       expected {};

        if ( owner_.compare_exchange_strong( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return;
        }
        if ( expected == tid ) {
            ++recursion_count_;
            return;
        }
        lock_slow( tid );
    }

    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        const std::thread::id tid = std::this_thread::get_id();
        std::thread::id       expected {};

        if ( owner_.compare_exchange_strong( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return true;
        }
        if ( expected == tid ) {
            ++recursion_count_;
            return true;
        }
        return false;
    }

    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        if ( --recursion_count_ == 0 )
            owner_.store( std::thread::id {}, std::memory_order_release );
    }

protected:
    std::atomic< std::thread::id > owner_ { std::thread::id {} };
    std::size_t                    recursion_count_ { 0 };

private:
    void lock_slow( std::thread::id tid ) noexcept;
};

/// @brief Recursive spinlock with exponential backoff.
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY recursive_spinlock_backoff :
    protected recursive_spinlock_plain
{
public:
    recursive_spinlock_backoff()                                               = default;
    recursive_spinlock_backoff( const recursive_spinlock_backoff& )            = delete;
    recursive_spinlock_backoff& operator=( const recursive_spinlock_backoff& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        const std::thread::id tid = std::this_thread::get_id();
        std::thread::id       expected {};

        if ( owner_.compare_exchange_strong( expected, tid, std::memory_order_acquire, std::memory_order_relaxed ) ) {
            recursion_count_ = 1;
            return;
        }
        if ( expected == tid ) {
            ++recursion_count_;
            return;
        }
        lock_slow( tid );
    }

    using recursive_spinlock_plain::try_lock;
    using recursive_spinlock_plain::unlock;

private:
    void lock_slow( std::thread::id tid ) noexcept;
};

/// @brief Shared (reader-writer) spinlock (CPU-pause hints only).
class NOVA_SYNC_CAPABILITY( "mutex" ) shared_spinlock_plain
{
public:
    shared_spinlock_plain()                                          = default;
    shared_spinlock_plain( const shared_spinlock_plain& )            = delete;
    shared_spinlock_plain& operator=( const shared_spinlock_plain& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_strong(
                 expected, write_locked, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow();
    }

    [[nodiscard]] inline bool try_lock() noexcept NOVA_SYNC_TRY_ACQUIRE( true )
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        if ( ( expected & readers_mask ) == 0 && ( expected & write_locked ) == 0 ) {
            uint32_t desired = ( expected & ~write_pending ) | write_locked;
            return state_.compare_exchange_strong( expected,
                                                   desired,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed );
        }
        return false;
    }

    inline void unlock() noexcept NOVA_SYNC_RELEASE()
    {
        state_.fetch_and( ~write_locked, std::memory_order_release );
    }

    inline void lock_shared() noexcept NOVA_SYNC_ACQUIRE_SHARED()
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        if ( ( expected & ( write_locked | write_pending ) ) == 0 ) {
            if ( state_.compare_exchange_strong(
                     expected, expected + 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        }
        lock_shared_slow();
    }

    [[nodiscard]] inline bool try_lock_shared() noexcept NOVA_SYNC_TRY_ACQUIRE_SHARED( true )
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        if ( ( expected & ( write_locked | write_pending ) ) == 0 ) {
            return state_.compare_exchange_strong( expected,
                                                   expected + 1,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed );
        }
        return false;
    }

    inline void unlock_shared() noexcept NOVA_SYNC_RELEASE_SHARED()
    {
        state_.fetch_sub( 1, std::memory_order_release );
    }

protected:
    static constexpr uint32_t write_locked  = 1U << 31;
    static constexpr uint32_t write_pending = 1U << 30;
    static constexpr uint32_t readers_mask  = ~( write_locked | write_pending );

    std::atomic< uint32_t > state_ { 0 };

private:
    void lock_slow() noexcept;
    void lock_shared_slow() noexcept;
};

/// @brief Shared (reader-writer) spinlock with exponential backoff.
class NOVA_SYNC_CAPABILITY( "mutex" ) shared_spinlock_backoff : protected shared_spinlock_plain
{
public:
    shared_spinlock_backoff()                                            = default;
    shared_spinlock_backoff( const shared_spinlock_backoff& )            = delete;
    shared_spinlock_backoff& operator=( const shared_spinlock_backoff& ) = delete;

    inline void lock() noexcept NOVA_SYNC_ACQUIRE()
    {
        uint32_t expected = 0;
        if ( state_.compare_exchange_strong(
                 expected, write_locked, std::memory_order_acquire, std::memory_order_relaxed ) )
            return;
        lock_slow();
    }

    inline void lock_shared() noexcept NOVA_SYNC_ACQUIRE_SHARED()
    {
        uint32_t expected = state_.load( std::memory_order_relaxed );
        if ( ( expected & ( write_locked | write_pending ) ) == 0 ) {
            if ( state_.compare_exchange_strong(
                     expected, expected + 1, std::memory_order_acquire, std::memory_order_relaxed ) )
                return;
        }
        lock_shared_slow();
    }

    using shared_spinlock_plain::try_lock;
    using shared_spinlock_plain::try_lock_shared;
    using shared_spinlock_plain::unlock;
    using shared_spinlock_plain::unlock_shared;

private:
    void lock_slow() noexcept;
    void lock_shared_slow() noexcept;
};

} // namespace impl

//----------------------------------------------------------------------------------------------------------------------

namespace detail {

using spinlock_allowed_tags = std::tuple< exponential_backoff_tag, recursive_tag, shared_tag >;

} // namespace detail

//----------------------------------------------------------------------------------------------------------------------

/// @brief Spinlock mutex with optional policies for backoff, recursive, or shared locking.
///
/// `recursive` and `shared` are mutually exclusive.
///
/// Policy parameters (from `nova/sync/mutex/policies.hpp`):
///
/// | Policy         | Effect                                                        |
/// |----------------|---------------------------------------------------------------|
/// | `with_backoff` | Exponential backoff with CPU pause hints before re-testing.  |
/// | `recursive`    | Allow re-entrant locking from the owning thread.              |
/// | `shared`       | Enable shared (reader-writer) locking via lock_shared().     |
///
/// Without any policy, spins using only CPU pause hints.
template < typename... Policies >
    requires( parameter::valid_parameters< detail::spinlock_allowed_tags, Policies... >
              && !(detail::has_recursive_v< Policies... > && detail::has_shared_v< Policies... >))
class NOVA_SYNC_CAPABILITY( "mutex" ) spinlock_mutex :
    public std::conditional_t<
        detail::has_recursive_v< Policies... >,
        std::conditional_t< detail::has_backoff_v< Policies... >, impl::recursive_spinlock_backoff, impl::recursive_spinlock_plain >,
        std::conditional_t<
            detail::has_shared_v< Policies... >,
            std::conditional_t< detail::has_backoff_v< Policies... >, impl::shared_spinlock_backoff, impl::shared_spinlock_plain >,
            std::conditional_t< detail::has_backoff_v< Policies... >, impl::spinlock_backoff, impl::spinlock_plain > > >
{};

/// @brief Recursive spinlock alias. Prefer `spinlock_mutex<recursive>`.
template < typename... Policies >
using recursive_spinlock_mutex = spinlock_mutex< recursive, Policies... >;

/// @brief Shared spinlock alias. Prefer `spinlock_mutex<shared>`.
template < typename... Policies >
using shared_spinlock_mutex = spinlock_mutex< shared, Policies... >;


//----------------------------------------------------------------------------------------------------------------------
// concepts_is_recursive / concepts_is_shared specializations

namespace concepts {

template < typename... Policies >
    requires( detail::has_recursive_v< Policies... > )
struct concepts_is_recursive< nova::sync::spinlock_mutex< Policies... > > : std::true_type
{};

} // namespace concepts

} // namespace nova::sync
