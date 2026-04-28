// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/thread_safety/macros.hpp>

namespace nova::sync {

namespace detail {

template < typename T >
using optional_result_t = std::conditional_t< std::is_void_v< T >, bool, T >;

} // namespace detail

// ---------------------------------------------------------------------------
// locked_object_guard - exclusive lock for locked_object
// ---------------------------------------------------------------------------

/// @brief Exclusive lock guard for a `locked_object`.
///
/// Provides RAII-style access to the guarded value with exclusive lock semantics.
/// Constness of the reference depends on the template parameter T:
/// - For mutable T: `operator*()` returns `T&` (mutable access)
/// - For `const T`: `operator*()` returns `const T&` (const access)
///
/// Returned by `locked_object::lock()` (both const and non-const instances).
/// The lock is automatically released on destruction.
template < typename T, typename Mutex >
class locked_object_guard
{
    T*     ptr_;
    Mutex* mtx_;

public:
    locked_object_guard( T& value, Mutex& mtx ) noexcept :
        ptr_( &value ),
        mtx_( &mtx )
    {}

    ~locked_object_guard() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_ )
            mtx_->unlock();
    }

    locked_object_guard( locked_object_guard&& other ) noexcept :
        ptr_( other.ptr_ ),
        mtx_( other.mtx_ )
    {
        other.ptr_ = nullptr;
        other.mtx_ = nullptr;
    }

    locked_object_guard& operator=( locked_object_guard&& other ) noexcept NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( this != &other ) {
            if ( mtx_ )
                mtx_->unlock();
            ptr_       = other.ptr_;
            mtx_       = other.mtx_;
            other.ptr_ = nullptr;
            other.mtx_ = nullptr;
        }
        return *this;
    }

    locked_object_guard( const locked_object_guard& )            = delete;
    locked_object_guard& operator=( const locked_object_guard& ) = delete;

    T& operator*() const noexcept
    {
        return *ptr_;
    }
    T* operator->() const noexcept
    {
        return ptr_;
    }
    T* get() const noexcept
    {
        return ptr_;
    }
};

// ---------------------------------------------------------------------------
// shared_locked_object_guard - shared lock
// ---------------------------------------------------------------------------

/// @brief Shared lock guard for a `locked_object` (read-lock semantics).
///
/// Provides RAII-style read access to the guarded value with shared lock semantics.
/// Always provides const access via `operator*()` returning `const T&`.
///
/// Returned by `locked_object::lock_shared()`. The shared lock is automatically
/// released on destruction. Multiple threads may hold shared locks concurrently.
template < typename T, typename Mutex >
class shared_locked_object_guard
{
    const T* ptr_;
    Mutex*   mtx_;

public:
    shared_locked_object_guard( const T& value, Mutex& mtx ) noexcept :
        ptr_( &value ),
        mtx_( &mtx )
    {}

    ~shared_locked_object_guard() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_ )
            mtx_->unlock_shared();
    }

    shared_locked_object_guard( shared_locked_object_guard&& other ) noexcept :
        ptr_( other.ptr_ ),
        mtx_( other.mtx_ )
    {
        other.ptr_ = nullptr;
        other.mtx_ = nullptr;
    }

    shared_locked_object_guard& operator=( shared_locked_object_guard&& other ) noexcept NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( this != &other ) {
            if ( mtx_ )
                mtx_->unlock_shared();
            ptr_       = other.ptr_;
            mtx_       = other.mtx_;
            other.ptr_ = nullptr;
            other.mtx_ = nullptr;
        }
        return *this;
    }

    shared_locked_object_guard( const shared_locked_object_guard& )            = delete;
    shared_locked_object_guard& operator=( const shared_locked_object_guard& ) = delete;

    const T& operator*() const noexcept
    {
        return *ptr_;
    }
    const T* operator->() const noexcept
    {
        return ptr_;
    }
    const T* get() const noexcept
    {
        return ptr_;
    }
};

// ---------------------------------------------------------------------------
// locked_object - Rust-inspired Mutex<T> / RwLock<T> wrapper
// ---------------------------------------------------------------------------

/// @brief Thread-safe wrapper pairing a value with a mutex (Rust-inspired `Mutex<T>` pattern).
///
/// This type enforces that the guarded value T is only accessible through lock guards,
/// preventing unsynchronized access at compile time. The value is paired with a `mutable`
/// mutex, enabling const instances to acquire exclusive locks (useful for const methods
/// that need to perform cache updates or other internal mutations).
///
/// **Exclusive locking (mutual exclusion):**
/// - Non-const instance: `lock()` and `try_lock*()` return `locked_object_guard<T, M>`
///   with mutable `T&` access
/// - Const instance: `lock()` and `try_lock*()` return `locked_object_guard<const T, M>`
///   with const `T&` access (still an exclusive lock, allowing interior mutability patterns)
///
/// **Shared locking (read-write lock pattern):**
/// - `lock_shared()` and `try_lock_shared*()` (const only) return `shared_locked_object_guard<T, M>`
///   with const `T&` access. Requires mutex supporting shared-locking concepts
///   (e.g., `std::shared_mutex`).
///
/// **Higher-order functions:**
/// - `lock_and(fn)`, `try_lock_and(fn)`, `lock_shared_and(fn)` acquire the lock and invoke
///   a function with the guarded value, automatically releasing the lock on return.
///
/// @tparam T     The guarded value type.
/// @tparam Mutex The mutex type (default: std::mutex). Must model `concepts::basic_lockable`.
template < typename T, typename Mutex = std::mutex >
    requires concepts::basic_lockable< Mutex >
class locked_object
{
    mutable Mutex mtx_;
    T value_      NOVA_SYNC_GUARDED_BY( mtx_ );

    // Exclusive lock helpers (mutable) - internal implementation
    locked_object_guard< T, Mutex > do_lock_() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        mtx_.lock();
        return { value_, mtx_ };
    }

    std::optional< locked_object_guard< T, Mutex > > do_try_lock_() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock() )
            return locked_object_guard< T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    template < typename Rep, typename Period >
    std::optional< locked_object_guard< T, Mutex > >
    do_try_lock_for_( const std::chrono::duration< Rep, Period >& d ) NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_for( d ) )
            return locked_object_guard< T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    template < typename Clock, typename Duration >
    std::optional< locked_object_guard< T, Mutex > >
    do_try_lock_until_( const std::chrono::time_point< Clock, Duration >& tp ) NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_until( tp ) )
            return locked_object_guard< T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    // Exclusive lock helpers (const) - internal implementation
    // Returns const access via locked_object_guard<const T, M>
    locked_object_guard< const T, Mutex > do_lock_const_() const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        mtx_.lock();
        return { value_, mtx_ };
    }

    std::optional< locked_object_guard< const T, Mutex > > do_try_lock_const_() const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock() )
            return locked_object_guard< const T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    template < typename Rep, typename Period >
    std::optional< locked_object_guard< const T, Mutex > >
    do_try_lock_for_const_( const std::chrono::duration< Rep, Period >& d ) const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_for( d ) )
            return locked_object_guard< const T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    template < typename Clock, typename Duration >
    std::optional< locked_object_guard< const T, Mutex > > do_try_lock_until_const_(
        const std::chrono::time_point< Clock, Duration >& tp ) const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_until( tp ) )
            return locked_object_guard< const T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    // Shared lock helpers - internal implementation
    // Only available on const instances (shared_lockable concept)
    shared_locked_object_guard< T, Mutex > do_lock_shared_() const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        mtx_.lock_shared();
        return { value_, mtx_ };
    }

    std::optional< shared_locked_object_guard< T, Mutex > > do_try_lock_shared_() const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_shared() )
            return shared_locked_object_guard< T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    template < typename Rep, typename Period >
    std::optional< shared_locked_object_guard< T, Mutex > >
    do_try_lock_shared_for_( const std::chrono::duration< Rep, Period >& d ) const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_shared_for( d ) )
            return shared_locked_object_guard< T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

    template < typename Clock, typename Duration >
    std::optional< shared_locked_object_guard< T, Mutex > > do_try_lock_shared_until_(
        const std::chrono::time_point< Clock, Duration >& tp ) const NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( mtx_.try_lock_shared_until( tp ) )
            return shared_locked_object_guard< T, Mutex > { value_, mtx_ };
        return std::nullopt;
    }

public:
    using value_type = T;
    using mutex_type = Mutex;

    // -- Construction & Destruction ------------------------------------------

    locked_object()
        requires std::default_initializable< T >
        :
        value_()
    {}

    template < typename... Args >
        requires std::constructible_from< T, Args... >
    explicit locked_object( Args&&... args ) :
        value_( std::forward< Args >( args )... )
    {}

    template < typename... Args >
        requires std::constructible_from< T, Args... >
    explicit locked_object( Mutex mtx, Args&&... args ) :
        mtx_( std::move( mtx ) ),
        value_( std::forward< Args >( args )... )
    {}

    locked_object( const locked_object& )            = delete;
    locked_object& operator=( const locked_object& ) = delete;

    // -- Exclusive locking - non-const (mutable access) ---------------------

    /// @brief Acquires exclusive lock; returns guard with mutable access.
    [[nodiscard]] locked_object_guard< T, Mutex > lock()
    {
        return do_lock_();
    }

    /// @brief Tries to acquire exclusive lock without blocking.
    [[nodiscard]] std::optional< locked_object_guard< T, Mutex > > try_lock()
        requires concepts::lockable< Mutex >
    {
        return do_try_lock_();
    }

    /// @brief Tries to acquire exclusive lock with a timeout duration.
    template < typename Rep, typename Period >
    [[nodiscard]] std::optional< locked_object_guard< T, Mutex > >
    try_lock_for( const std::chrono::duration< Rep, Period >& duration )
        requires concepts::timed_lockable< Mutex >
    {
        return do_try_lock_for_( duration );
    }

    /// @brief Tries to acquire exclusive lock until a time point.
    template < typename Clock, typename Duration >
    [[nodiscard]] std::optional< locked_object_guard< T, Mutex > >
    try_lock_until( const std::chrono::time_point< Clock, Duration >& time_point )
        requires concepts::timed_lockable< Mutex >
    {
        return do_try_lock_until_( time_point );
    }

    // -- Exclusive locking - const (const access via exclusive lock) --------

    /// @brief Acquires exclusive lock on a const instance; returns guard with const access.
    [[nodiscard]] locked_object_guard< const T, Mutex > lock() const
    {
        return do_lock_const_();
    }

    /// @brief Tries to acquire exclusive lock without blocking (const instance).
    [[nodiscard]] std::optional< locked_object_guard< const T, Mutex > > try_lock() const
        requires concepts::lockable< Mutex >
    {
        return do_try_lock_const_();
    }

    /// @brief Tries to acquire exclusive lock with a timeout duration (const instance).
    template < typename Rep, typename Period >
    [[nodiscard]] std::optional< locked_object_guard< const T, Mutex > >
    try_lock_for( const std::chrono::duration< Rep, Period >& duration ) const
        requires concepts::timed_lockable< Mutex >
    {
        return do_try_lock_for_const_( duration );
    }

    /// @brief Tries to acquire exclusive lock until a time point (const instance).
    template < typename Clock, typename Duration >
    [[nodiscard]] std::optional< locked_object_guard< const T, Mutex > >
    try_lock_until( const std::chrono::time_point< Clock, Duration >& time_point ) const
        requires concepts::timed_lockable< Mutex >
    {
        return do_try_lock_until_const_( time_point );
    }

    // -- Shared locking (const) ---------------------------------------------

    /// @brief Acquires shared lock; returns guard with const access.
    [[nodiscard]] shared_locked_object_guard< T, Mutex > lock_shared() const
        requires concepts::shared_lockable< Mutex >
    {
        return do_lock_shared_();
    }

    /// @brief Tries to acquire shared lock without blocking.
    [[nodiscard]] std::optional< shared_locked_object_guard< T, Mutex > > try_lock_shared() const
        requires concepts::shared_lockable< Mutex >
    {
        return do_try_lock_shared_();
    }

    /// @brief Tries to acquire shared lock with a timeout duration.
    template < typename Rep, typename Period >
    [[nodiscard]] std::optional< shared_locked_object_guard< T, Mutex > >
    try_lock_shared_for( const std::chrono::duration< Rep, Period >& duration ) const
        requires concepts::shared_timed_lockable< Mutex >
    {
        return do_try_lock_shared_for_( duration );
    }

    /// @brief Tries to acquire shared lock until a time point.
    template < typename Clock, typename Duration >
    [[nodiscard]] std::optional< shared_locked_object_guard< T, Mutex > >
    try_lock_shared_until( const std::chrono::time_point< Clock, Duration >& time_point ) const
        requires concepts::shared_timed_lockable< Mutex >
    {
        return do_try_lock_shared_until_( time_point );
    }

    // -- Higher-order functions - non-const (mutable access) ----------------

    /// @brief Acquires exclusive lock and invokes fn with mutable reference.
    template < typename Fn >
    std::invoke_result_t< Fn, T& > lock_and( Fn&& fn )
    {
        auto guard = lock();
        return std::invoke( std::forward< Fn >( fn ), *guard );
    }

    /// @brief Tries to acquire exclusive lock and invokes fn if successful.
    template < typename Fn >
    std::optional< detail::optional_result_t< std::invoke_result_t< Fn, T& > > > try_lock_and( Fn&& fn )
        requires concepts::lockable< Mutex >
    {
        auto guard = try_lock();
        if ( !guard )
            return std::nullopt;
        if constexpr ( std::is_void_v< std::invoke_result_t< Fn, T& > > ) {
            std::invoke( std::forward< Fn >( fn ), **guard );
            return true;
        } else {
            return std::invoke( std::forward< Fn >( fn ), **guard );
        }
    }

    /// @brief Tries to acquire exclusive lock with timeout and invokes fn if successful.
    template < typename Fn, typename Rep, typename Period >
    std::optional< detail::optional_result_t< std::invoke_result_t< Fn, T& > > >
    try_lock_for_and( const std::chrono::duration< Rep, Period >& duration, Fn&& fn )
        requires concepts::timed_lockable< Mutex >
    {
        auto guard = try_lock_for( duration );
        if ( !guard )
            return std::nullopt;
        if constexpr ( std::is_void_v< std::invoke_result_t< Fn, T& > > ) {
            std::invoke( std::forward< Fn >( fn ), **guard );
            return true;
        } else {
            return std::invoke( std::forward< Fn >( fn ), **guard );
        }
    }

    // -- Higher-order functions - const (const access via exclusive lock) ---

    /// @brief Acquires exclusive lock on a const instance and invokes fn with const reference.
    template < typename Fn >
    std::invoke_result_t< Fn, const T& > lock_and( Fn&& fn ) const
    {
        auto guard = lock();
        return std::invoke( std::forward< Fn >( fn ), *guard );
    }

    /// @brief Tries to acquire exclusive lock (const instance) and invokes fn if successful.
    template < typename Fn >
    std::optional< detail::optional_result_t< std::invoke_result_t< Fn, const T& > > > try_lock_and( Fn&& fn ) const
        requires concepts::lockable< Mutex >
    {
        auto guard = try_lock();
        if ( !guard )
            return std::nullopt;
        if constexpr ( std::is_void_v< std::invoke_result_t< Fn, const T& > > ) {
            std::invoke( std::forward< Fn >( fn ), **guard );
            return true;
        } else {
            return std::invoke( std::forward< Fn >( fn ), **guard );
        }
    }

    /// @brief Tries to acquire exclusive lock with timeout (const instance) and invokes fn.
    template < typename Fn, typename Rep, typename Period >
    std::optional< detail::optional_result_t< std::invoke_result_t< Fn, const T& > > >
    try_lock_for_and( const std::chrono::duration< Rep, Period >& duration, Fn&& fn ) const
        requires concepts::timed_lockable< Mutex >
    {
        auto guard = try_lock_for( duration );
        if ( !guard )
            return std::nullopt;
        if constexpr ( std::is_void_v< std::invoke_result_t< Fn, const T& > > ) {
            std::invoke( std::forward< Fn >( fn ), **guard );
            return true;
        } else {
            return std::invoke( std::forward< Fn >( fn ), **guard );
        }
    }

    // -- Higher-order functions - shared ------------------------------------

    /// @brief Acquires shared lock and invokes fn with const reference.
    template < typename Fn >
    std::invoke_result_t< Fn, const T& > lock_shared_and( Fn&& fn ) const
        requires concepts::shared_lockable< Mutex >
    {
        auto guard = lock_shared();
        return std::invoke( std::forward< Fn >( fn ), *guard );
    }

    /// @brief Tries to acquire shared lock and invokes fn if successful.
    template < typename Fn >
    std::optional< detail::optional_result_t< std::invoke_result_t< Fn, const T& > > > try_lock_shared_and( Fn&& fn ) const
        requires concepts::shared_lockable< Mutex >
    {
        auto guard = try_lock_shared();
        if ( !guard )
            return std::nullopt;
        if constexpr ( std::is_void_v< std::invoke_result_t< Fn, const T& > > ) {
            std::invoke( std::forward< Fn >( fn ), **guard );
            return true;
        } else {
            return std::invoke( std::forward< Fn >( fn ), **guard );
        }
    }
};

} // namespace nova::sync
