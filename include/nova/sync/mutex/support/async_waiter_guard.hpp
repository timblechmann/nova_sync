// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/concepts.hpp>

namespace nova::sync::detail {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct adopt_async_waiter_t
{
    explicit adopt_async_waiter_t() = default;
};

inline constexpr adopt_async_waiter_t adopt_async_waiter {};

/// @brief RAII guard that registers an async waiter on construction and
///        unregisters it on destruction (or on an explicit `release()` call).
///
/// ### Acquiring ownership
///
/// Use `try_acquire()` (not `try_lock()` + manual `consume_lock()`) when the
/// guard is active:
///
/// @code
///   async_waiter_guard guard(mtx);        // add_async_waiter() called
///   // ... wait on native handle ...
///   if (guard.try_acquire()) {            // try_lock() + consume_lock()
///       // guard is now released; lock is owned
///       // ... critical section ...
///       mtx.unlock();
///   }
///   // if not acquired the guard will release on destruction
/// @endcode
///
/// ### Timeout / cancellation
///
/// @code
///   async_waiter_guard guard(mtx);
///   // ... wait on native handle — timed out ...
///   guard.release();                      // remove_async_waiter() called
///   // no lock acquired
/// @endcode
template < typename Mutex >
class async_waiter_guard
{
public:
    explicit async_waiter_guard( Mutex& mtx ) noexcept :
        mtx_( mtx )
    {
        if constexpr ( concepts::async_waiter_mutex< Mutex > )
            mtx_.add_async_waiter();
    }

    /// @brief Adopting constructor — takes ownership of an already-registered waiter.
    ///
    /// Use this when `add_async_waiter()` was called manually (e.g. to capture
    /// its return value) and you want the guard to handle `remove_async_waiter()`
    /// on all exit paths.
    ///
    /// @code
    ///   uint32_t s = mtx.add_async_waiter(); // register and capture state
    ///   async_waiter_guard guard(mtx, adopt_async_waiter);
    ///   // guard will call remove_async_waiter() on exit
    /// @endcode
    async_waiter_guard( Mutex& mtx, adopt_async_waiter_t ) noexcept :
        mtx_( mtx )
    {
        // add_async_waiter() already called by the caller; do not call again.
    }

    ~async_waiter_guard() noexcept
    {
        release();
    }

    [[nodiscard]] bool try_acquire() noexcept
    {
        if ( !mtx_.try_lock() )
            return false;

        // Drain any stale kernel notification posted by unlock() between
        // our waiter registration and the successful try_lock() CAS.
        if constexpr ( concepts::async_waiter_mutex< Mutex > )
            mtx_.consume_lock();

        release();
        return true;
    }

    void release() noexcept
    {
        if ( active_ ) {
            active_ = false;
            if constexpr ( concepts::async_waiter_mutex< Mutex > )
                mtx_.remove_async_waiter();
        }
    }

    void dismiss() noexcept
    {
        active_ = false;
    }

    [[nodiscard]] bool active() const noexcept
    {
        return active_;
    }

    // Non-copyable, non-movable.
    async_waiter_guard( const async_waiter_guard& )            = delete;
    async_waiter_guard& operator=( const async_waiter_guard& ) = delete;
    async_waiter_guard( async_waiter_guard&& )                 = delete;
    async_waiter_guard& operator=( async_waiter_guard&& )      = delete;

private:
    Mutex& mtx_;
    bool   active_ { true };
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace nova::sync::detail
