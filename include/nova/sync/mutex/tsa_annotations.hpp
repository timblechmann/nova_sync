// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/tsa_macros.hpp>

// ---------------------------------------------------------------------------
// tsa_mutex_adapter — annotated wrapper for mutex types.
//
// Usage:
//   nova::sync::tsa_mutex_adapter<std::mutex> mtx;
//   int value NOVA_SYNC_GUARDED_BY(mtx) = 0;
// ---------------------------------------------------------------------------

#include <utility>

#include <chrono>
#include <nova/sync/mutex/concepts.hpp>

namespace nova::sync {

/// @brief Annotated wrapper for any `BasicLockable` mutex type.
///
/// Exposes the standard lock APIs with Clang thread-safety annotations.
template < concepts::basic_lockable Mutex >
class NOVA_SYNC_CAPABILITY( "mutex" ) tsa_mutex_adapter
{
public:
    /// @brief Constructs the adapter, forwarding @p args to the underlying mutex.
    template < typename... Args >
    explicit tsa_mutex_adapter( Args&&... args ) :
        mtx_( std::forward< Args >( args )... )
    {}

    tsa_mutex_adapter( const tsa_mutex_adapter& )            = delete;
    tsa_mutex_adapter& operator=( const tsa_mutex_adapter& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() NOVA_SYNC_ACQUIRE()
    {
        mtx_.lock();
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock()
        requires concepts::lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return mtx_.try_lock();
    }

    /// @brief Releases the lock.
    void unlock() NOVA_SYNC_RELEASE()
    {
        mtx_.unlock();
    }

    // -----------------------------------------------------------------------
    // Shared locking API

    /// @brief Acquires a shared read lock.
    void lock_shared()
        requires concepts::shared_lockable< Mutex >
    NOVA_SYNC_ACQUIRE_SHARED()
    {
        mtx_.lock_shared();
    }

    /// @brief Attempts to acquire a shared read lock without blocking.
    [[nodiscard]] bool try_lock_shared()
        requires concepts::shared_lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE_SHARED( true )
    {
        return mtx_.try_lock_shared();
    }

    /// @brief Releases a shared read lock.
    void unlock_shared()
        requires concepts::shared_lockable< Mutex >
    NOVA_SYNC_RELEASE_SHARED()
    {
        mtx_.unlock_shared();
    }

    // -----------------------------------------------------------------------
    // Timed locking API

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    template < class Rep, class Period >
    [[nodiscard]] bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time )
        requires concepts::timed_lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return mtx_.try_lock_for( rel_time );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time )
        requires concepts::timed_lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return mtx_.try_lock_until( abs_time );
    }

    /// @brief Returns a reference to the underlying mutex.
    Mutex& native_handle() noexcept
    {
        return mtx_;
    }

private:
    Mutex mtx_;
};

/// @brief Annotated wrapper for re-entrant `BasicLockable` mutex types.
///
/// Exposes the standard lock APIs with Clang thread-safety annotations and
/// marks the adapter as reentrant via NOVA_SYNC_REENTRANT_CAPABILITY.
/// Note: `tsa_recursive_mutex_adapter` requires Clang 22 or newer for
/// proper recursive-capability support in the analyzer.
template < concepts::recursive_mutex Mutex >
class NOVA_SYNC_CAPABILITY( "mutex" ) NOVA_SYNC_REENTRANT_CAPABILITY tsa_recursive_mutex_adapter
{
public:
    /// @brief Constructs the adapter, forwarding @p args to the underlying mutex.
    template < typename... Args >
    explicit tsa_recursive_mutex_adapter( Args&&... args ) :
        mtx_( std::forward< Args >( args )... )
    {}

    tsa_recursive_mutex_adapter( const tsa_recursive_mutex_adapter& )            = delete;
    tsa_recursive_mutex_adapter& operator=( const tsa_recursive_mutex_adapter& ) = delete;

    /// @brief Acquires the lock, blocking as necessary.
    void lock() NOVA_SYNC_ACQUIRE()
    {
        mtx_.lock();
    }

    /// @brief Attempts to acquire the lock without blocking.
    /// @return `true` if lock acquired, `false` if already locked.
    [[nodiscard]] bool try_lock()
        requires concepts::lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return mtx_.try_lock();
    }

    /// @brief Releases the lock.
    void unlock() NOVA_SYNC_RELEASE()
    {
        mtx_.unlock();
    }

    // -----------------------------------------------------------------------
    // Shared locking (forwarded when the underlying type supports it)

    /// @brief Acquires a shared read lock.
    void lock_shared()
        requires concepts::shared_lockable< Mutex >
    NOVA_SYNC_ACQUIRE_SHARED()
    {
        mtx_.lock_shared();
    }

    /// @brief Attempts to acquire a shared read lock without blocking.
    [[nodiscard]] bool try_lock_shared()
        requires concepts::shared_lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE_SHARED( true )
    {
        return mtx_.try_lock_shared();
    }

    /// @brief Releases a shared read lock.
    void unlock_shared()
        requires concepts::shared_lockable< Mutex >
    NOVA_SYNC_RELEASE_SHARED()
    {
        mtx_.unlock_shared();
    }

    // -----------------------------------------------------------------------
    // Timed locking (forwarded when the underlying type supports it)

    /// @brief Attempts to acquire the lock, blocking for up to @p rel_time.
    template < class Rep, class Period >
    [[nodiscard]] bool try_lock_for( const std::chrono::duration< Rep, Period >& rel_time )
        requires concepts::timed_lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return mtx_.try_lock_for( rel_time );
    }

    /// @brief Attempts to acquire the lock, blocking until @p abs_time.
    template < class Clock, class Duration >
    [[nodiscard]] bool try_lock_until( const std::chrono::time_point< Clock, Duration >& abs_time )
        requires concepts::timed_lockable< Mutex >
    NOVA_SYNC_TRY_ACQUIRE( true )
    {
        return mtx_.try_lock_until( abs_time );
    }

    /// @brief Returns a reference to the underlying mutex.
    Mutex& native_handle() noexcept
    {
        return mtx_;
    }

private:
    Mutex mtx_;
};

// ---------------------------------------------------------------------------
// lock_guard — RAII exclusive lock with TSA annotations.
// ---------------------------------------------------------------------------

/// @brief RAII exclusive lock guard with Clang thread safety annotations.
template < typename Mutex >
class NOVA_SYNC_SCOPED_LOCKABLE lock_guard
{
public:
    /// @brief Locks @p m on construction.
    explicit lock_guard( Mutex& m ) NOVA_SYNC_ACQUIRE( m ) :
        mtx_( m )
    {
        mtx_.lock();
    }

    /// @brief Adopts an already-locked @p m (does not lock).
    lock_guard( Mutex& m, std::adopt_lock_t ) NOVA_SYNC_REQUIRES( m ) :
        mtx_( m )
    {}

    /// @brief Unlocks the mutex.
    ~lock_guard() NOVA_SYNC_RELEASE()
    {
        mtx_.unlock();
    }

    lock_guard( const lock_guard& )            = delete;
    lock_guard& operator=( const lock_guard& ) = delete;

private:
    Mutex& mtx_;
};

} // namespace nova::sync
