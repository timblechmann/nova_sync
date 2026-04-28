// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file libdispatch_support.hpp
///
/// Asynchronous mutex acquisition helpers built on Apple libdispatch
/// (Grand Central Dispatch).
///
/// Handler signature
/// -----------------
/// All callbacks follow the same convention:
///
/// @code
///   void handler(expected<std::unique_lock<Mutex>, std::error_code> result);
/// @endcode
///
/// where `expected` is `std::expected` (C++23) or `tl::expected` (C++20+).
///
/// - **Success** (`result.has_value()`): Lock is owned; `result->owns_lock() == true`.
///   Call `result->unlock()` (or let it destruct) to release.
/// - **Cancellation / error** (`!result.has_value()`): Lock is not owned;
///   `result.error()` contains the error code.
///
/// Example
/// -------
/// @code
///   nova::sync::native_async_mutex mtx;
///   dispatch_queue_t queue = dispatch_queue_create("com.example.q",
///                                                  DISPATCH_QUEUE_SERIAL);
///
///   nova::sync::async_acquire(mtx, queue,
///       [](auto result) {
///           if (result) {
///               // lock acquired
///           }
///       });
/// @endcode

#include <nova/sync/mutex/detail/async_support.hpp>
#include <nova/sync/mutex/tsa_annotations.hpp>

#if defined( __APPLE__ ) && defined( NOVA_SYNC_HAS_EXPECTED )

#  include <atomic>
#  include <future>
#  include <memory>
#  include <mutex>
#  include <optional>
#  include <system_error>
#  include <utility>

#  include <dispatch/dispatch.h>

#  include <nova/sync/mutex/concepts.hpp>
#  include <nova/sync/mutex/detail/async_support.hpp>

namespace nova::sync {

// ---------------------------------------------------------------------------
// detail
// ---------------------------------------------------------------------------

namespace detail {

#  if !__has_feature( objc_arc )
inline void release_dispatch_object( dispatch_object_t&& obj ) noexcept
{
    if ( obj )
        dispatch_release( obj );
    obj = nullptr;
}
#  else
inline void release_dispatch_object( dispatch_object_t&& obj ) noexcept
{
    obj = nullptr;
}
#  endif


// ---------------------------------------------------------------------------
// Shared cancellation state (non-cancellable path does not use this).
// ---------------------------------------------------------------------------

struct dispatch_cancel_state
{
    std::atomic< bool >     cancelled { false };
    std::atomic< bool >     handler_invoked { false }; // Track if we've delivered a result
    std::function< void() > cancellation_callback_;

    void cancel() noexcept
    {
        cancelled.store( true, std::memory_order_release );
        if ( cancellation_callback_ )
            cancellation_callback_();
    }
    bool is_cancelled() const noexcept
    {
        return cancelled.load( std::memory_order_acquire );
    }
    bool try_mark_invoked() noexcept
    {
        bool expected = false;
        return handler_invoked.compare_exchange_strong( expected,
                                                        true,
                                                        std::memory_order_release,
                                                        std::memory_order_acquire );
    }
};

/// @brief Non-cancellable async acquire state: holds a single persistent dispatch source.
///
/// The dispatch source is created once and kept active across retry loops.
/// It is only released when the lock is successfully acquired or an error occurs.
template < typename Mutex, typename Handler >
struct dispatch_acquire_state : std::enable_shared_from_this< dispatch_acquire_state< Mutex, Handler > >
{
    Mutex&                                               mtx_;
    dispatch_queue_t                                     queue_;
    Handler                                              handler_;
    dispatch_source_t                                    src_ {};
    std::optional< detail::async_waiter_guard< Mutex > > waiter_guard_;

    dispatch_acquire_state( Mutex& mtx, dispatch_queue_t queue, Handler&& handler ) :
        mtx_( mtx ),
        queue_( queue ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~dispatch_acquire_state()
    {
        if ( src_ )
            release_dispatch_object( src_ );
    }

    // Async acquire: lock ownership is transferred to the lambda/handler,
    // so the analyzer cannot track it across the dispatch boundary.
    NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS void start()
    {
        // Try fast path first
        if ( mtx_.try_lock() ) {
            dispatch_async( queue_, [ self = this->shared_from_this() ]() {
                invoke_with_lock( self->handler_, std::unique_lock< Mutex >( self->mtx_, std::adopt_lock ) );
            } );
            return;
        }

        // Register as a waiter so unlock() will trigger the kevent
        waiter_guard_.emplace( mtx_ );

        // Create the dispatch source
        src_ = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ,
                                       static_cast< uintptr_t >( mtx_.native_handle() ),
                                       0,
                                       queue_ );

        // Set the event handler - tries to acquire on each event
        dispatch_source_set_event_handler( src_, [ self = this->shared_from_this() ]() {
            if ( self->waiter_guard_->try_acquire() ) {
                // Successfully acquired! Cancel source (triggers cleanup) and dispatch handler
                dispatch_source_cancel( self->src_ );
                dispatch_async( self->queue_, [ self ]() {
                    invoke_with_lock( self->handler_, std::unique_lock< Mutex >( self->mtx_, std::adopt_lock ) );
                } );
            }
            // Otherwise: source stays active, wait for next event
        } );

        // Set cancel handler to clean up the source
        dispatch_source_set_cancel_handler( src_, [ self = this->shared_from_this() ]() {
            if ( self->src_ ) {
                detail::release_dispatch_object( self->src_ );
                self->src_ = nullptr;
            }
        } );

        dispatch_resume( src_ );
    }
};

template < typename Mutex, typename Handler >
struct dispatch_acquire_cancellable_state :
    std::enable_shared_from_this< dispatch_acquire_cancellable_state< Mutex, Handler > >
{
    Mutex&                                               mtx_;
    dispatch_queue_t                                     queue_;
    Handler                                              handler_;
    dispatch_source_t                                    src_ {};
    const std::shared_ptr< dispatch_cancel_state >       cancel_state_;
    std::optional< detail::async_waiter_guard< Mutex > > waiter_guard_;

    dispatch_acquire_cancellable_state( Mutex& mtx, dispatch_queue_t queue, Handler&& handler ) :
        mtx_( mtx ),
        queue_( queue ),
        handler_( std::forward< Handler >( handler ) ),
        cancel_state_( std::make_shared< dispatch_cancel_state >() )
    {}

    ~dispatch_acquire_cancellable_state()
    {
        if ( src_ )
            release_dispatch_object( src_ );
    }

    // Async acquire: lock ownership is transferred across dispatch boundaries;
    // the analyzer cannot track it, so we opt out here.
    NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS void start()
    {
        // Install cancellation callback to actively cancel the source
        cancel_state_->cancellation_callback_ = [ self = this->shared_from_this() ] {
            if ( self->src_ )
                dispatch_source_cancel( self->src_ );
        };

        // Check if already cancelled
        if ( cancel_state_->is_cancelled() ) {
            if ( cancel_state_->try_mark_invoked() )
                dispatch_async( queue_, [ self = this->shared_from_this() ] {
                    invoke_with_error< Mutex >( self->handler_, std::errc::operation_canceled );
                } );

            return;
        }

        // Try fast path first
        if ( mtx_.try_lock() ) {
            // Double-check cancel flag after acquiring
            if ( cancel_state_->is_cancelled() ) {
                mtx_.unlock();
                if ( cancel_state_->try_mark_invoked() ) {
                    dispatch_async( queue_, [ self = this->shared_from_this() ] {
                        invoke_with_error< Mutex >( self->handler_, std::errc::operation_canceled );
                    } );
                }
                return;
            }

            if ( cancel_state_->try_mark_invoked() )
                dispatch_async( queue_, [ self = this->shared_from_this() ] {
                    invoke_with_lock( self->handler_, std::unique_lock( self->mtx_, std::adopt_lock ) );
                } );

            return;
        }

        // Register as a waiter so unlock() will trigger the kevent
        waiter_guard_.emplace( mtx_ );

        // Create the dispatch source
        src_ = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ, uintptr_t( mtx_.native_handle() ), 0, queue_ );

        // Set the event handler - tries to acquire on each event
        dispatch_source_set_event_handler( src_,
                                           [ self = this->shared_from_this() ]() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS {
            if ( self->cancel_state_->is_cancelled() ) {
                // Cancelled: release source and deliver error
                if ( self->cancel_state_->try_mark_invoked() ) {
                    self->waiter_guard_.reset();
                    dispatch_source_cancel( self->src_ );
                    dispatch_async( self->queue_, [ self ] {
                        invoke_with_error< Mutex >( self->handler_, std::errc::operation_canceled );
                    } );
                }
                return;
            }

            if ( self->waiter_guard_->try_acquire() ) {
                // Lock acquired; guard is already released by try_acquire().
                // Double-check cancel flag after acquiring.
                if ( self->cancel_state_->is_cancelled() ) {
                    self->mtx_.unlock();
                    if ( self->cancel_state_->try_mark_invoked() ) {
                        dispatch_source_cancel( self->src_ );
                        dispatch_async( self->queue_, [ self ] {
                            invoke_with_error< Mutex >( self->handler_, std::errc::operation_canceled );
                        } );
                    }
                    return;
                }

                // Successfully acquired! Cancel source and dispatch handler
                if ( self->cancel_state_->try_mark_invoked() ) {
                    dispatch_source_cancel( self->src_ );
                    dispatch_async( self->queue_, [ self ]() {
                        invoke_with_lock( self->handler_, std::unique_lock( self->mtx_, std::adopt_lock ) );
                    } );
                }
            }
        } );

        // Set cancel handler to clean up the source
        dispatch_source_set_cancel_handler( src_, [ self = this->shared_from_this() ] {
            if ( self->src_ ) {
                detail::release_dispatch_object( self->src_ );
                self->src_ = nullptr;
            }
            // Ensure handler is invoked if we're being cancelled and haven't delivered yet
            if ( self->cancel_state_->is_cancelled() && self->cancel_state_->try_mark_invoked() ) {
                self->waiter_guard_.reset();
                dispatch_async( self->queue_, [ self ]() {
                    invoke_with_error< Mutex >( self->handler_, std::errc::operation_canceled );
                } );
            }
        } );

        dispatch_resume( src_ );
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Public API — cancellable handle
// ---------------------------------------------------------------------------

/// @brief Opaque handle returned by `async_acquire_cancellable`.
///
/// Exposes a single `cancel()` method.  Destroying the handle (explicitly or
/// by going out of scope) automatically cancels any pending wait and causes
/// the handler to be called with `errc::operation_canceled`.  No explicit
/// cleanup is required.
///
/// Thread safety: `cancel()` and the destructor are safe to call from any
/// thread or queue.
///
/// @note After the handler has already been invoked `cancel()` is a no-op.
class dispatch_acquire_handle
{
public:
    dispatch_acquire_handle() = default;

    explicit dispatch_acquire_handle( std::shared_ptr< detail::dispatch_cancel_state > s ) :
        state_( std::move( s ) )
    {}

    // Move semantics: ownership of the cancellation state transfers.
    dispatch_acquire_handle( dispatch_acquire_handle&& other ) noexcept :
        state_( std::move( other.state_ ) )
    {}
    dispatch_acquire_handle& operator=( dispatch_acquire_handle&& other ) noexcept
    {
        if ( this != &other ) {
            cancel(); // cancel the old operation first
            state_ = std::move( other.state_ );
        }
        return *this;
    }

    // Non-copyable.
    dispatch_acquire_handle( const dispatch_acquire_handle& )            = delete;
    dispatch_acquire_handle& operator=( const dispatch_acquire_handle& ) = delete;

    /// @brief Requests cancellation of the pending acquire.
    ///
    /// Sets an internal flag.  The next retry point in the operation chain
    /// will detect it and deliver `errc::operation_canceled` to the handler.
    /// If the lock was grabbed just before the flag was seen it is released.
    /// Safe to call from any thread or queue. Idempotent.
    void cancel()
    {
        if ( state_ )
            state_->cancel();
    }

    /// @brief Returns `true` if associated with an in-flight operation.
    explicit operator bool() const noexcept
    {
        return state_ != nullptr;
    }

    /// @brief Destructor: automatically cancels the pending wait if not done.
    ///
    /// The handler will be called with `errc::operation_canceled` if the
    /// operation is still in flight.  This ensures that the handler is always
    /// invoked exactly once, either with a locked mutex or with an error.
    ~dispatch_acquire_handle()
    {
        cancel();
    }

private:
    std::shared_ptr< detail::dispatch_cancel_state > state_;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// @brief Asynchronously acquires @p mtx on @p queue and invokes @p handler
///        with an `expected<unique_lock, error_code>` result.
///
/// The handler is called exactly once, on @p queue, with the signature:
///
/// @code
///   void handler(expected<std::unique_lock<Mutex>, std::error_code> result);
/// @endcode
///
/// - **Success** (`result.has_value()`): Lock is owned; `result->owns_lock() == true`.
/// - **Cancellation / error** (`!result.has_value()`): `result.error()` holds the error.
///
/// Lifetime
/// --------
/// - @p mtx must remain live until @p handler fires.
/// - @p queue must remain live until @p handler fires.
///
/// @param mtx     A mutex satisfying `native_async_mutex` with `native_handle()`.
/// @param queue   Dispatch queue for event delivery and @p handler.
/// @param handler Callable invoked with `expected<std::unique_lock<Mutex>, std::error_code>`.
template < typename Mutex, typename Handler >
void async_acquire( Mutex& mtx, dispatch_queue_t queue, Handler&& handler )
    requires detail::invocable_with_expected< Handler, Mutex > && concepts::native_async_mutex< Mutex >
{
    using state_type = detail::dispatch_acquire_state< Mutex, std::decay_t< Handler > >;
    auto state       = std::make_shared< state_type >( mtx, queue, std::forward< Handler >( handler ) );
    state->start();
}

/// @brief Asynchronously acquires @p mtx on @p queue and returns a
///        cancellable handle.
///
/// Identical to the non-cancellable `async_acquire` overload except:
///
/// - Returns a `dispatch_acquire_handle` exposing a `cancel()` method.
/// - Calling `handle.cancel()` aborts the wait; @p handler is called with
///   `errc::operation_canceled`.
/// - Destroying the handle automatically cancels any pending wait.
///
/// Example
/// -------
/// @code
///   auto handle = nova::sync::async_acquire_cancellable(mtx, queue,
///       [](auto result) {
///           if (result) {
///               // lock acquired
///           } else if (result.error() == std::errc::operation_canceled) {
///               // cancelled
///           }
///       });
///   handle.cancel();
/// @endcode
template < typename Mutex, typename Handler >
dispatch_acquire_handle async_acquire_cancellable( Mutex& mtx, dispatch_queue_t queue, Handler&& handler )
    requires detail::invocable_with_expected< Handler, Mutex > && concepts::native_async_mutex< Mutex >
{
    using state_type = detail::dispatch_acquire_cancellable_state< Mutex, std::decay_t< Handler > >;
    auto op          = std::make_shared< state_type >( mtx, queue, std::forward< Handler >( handler ) );
    op->start();

    return dispatch_acquire_handle {
        op->cancel_state_,
    };
}

/// @brief Asynchronously acquires @p mtx on @p queue and returns a future.
///
/// If the mutex is immediately available (`try_lock()` succeeds), the returned
/// future is already ready with a locked `unique_lock`. Otherwise the acquisition
/// is performed asynchronously; once the mutex is acquired the future becomes ready.
///
/// @param mtx   A mutex satisfying `native_async_mutex` with `native_handle()`.
/// @param queue Dispatch queue for event delivery.
template < typename Mutex >
struct dispatch_acquire_future_state
{
    std::future< std::unique_lock< Mutex > > future;
};

template < typename Mutex >
NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS dispatch_acquire_future_state< Mutex > async_acquire( Mutex&           mtx,
                                                                                          dispatch_queue_t queue )
    requires concepts::native_async_mutex< Mutex >
{
    using promise_t = std::promise< std::unique_lock< Mutex > >;

    if ( mtx.try_lock() ) {
        promise_t promise;
        promise.set_value( std::unique_lock( mtx, std::adopt_lock ) );
        return dispatch_acquire_future_state {
            promise.get_future(),
        };
    }

    // Register as a waiter so unlock() will trigger the kevent (fast-path types only)
    auto promise    = std::make_shared< promise_t >();
    auto future     = promise->get_future();
    auto retry_func = std::make_shared< std::function< void() > >();
    auto wg         = std::make_shared< detail::async_waiter_guard< Mutex > >( mtx );

    *retry_func = [ &mtx, promise, retry_func, queue, wg ]() {
        // Create a new dispatch source for this wait attempt
        dispatch_source_t src = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ,
                                                        static_cast< uintptr_t >( mtx.native_handle() ),
                                                        0,
                                                        queue );

        dispatch_source_set_event_handler( src, [ &mtx, promise, retry_func, src, queue, wg ] {
            if ( wg->try_acquire() ) {
                // Successfully acquired — set promise and clean up
                dispatch_source_cancel( src );
                dispatch_async( dispatch_get_global_queue( QOS_CLASS_DEFAULT, 0 ), [ promise, &mtx ] {
                    promise->set_value( std::unique_lock< Mutex >( mtx, std::adopt_lock ) );
                } );
            } else {
                // Spurious wakeup — retry
                dispatch_source_cancel( src );
                dispatch_async( queue, [ retry_func ] {
                    ( *retry_func )();
                } );
            }
        } );

        dispatch_source_set_cancel_handler( src, [ src, &mtx, wg ] {
            detail::release_dispatch_object( src );
        } );

        dispatch_resume( src );
    };

    // Start the first wait attempt
    dispatch_async( queue, [ retry_func ] {
        ( *retry_func )();
    } );

    return dispatch_acquire_future_state {
        std::move( future ),
    };
}

} // namespace nova::sync


#endif // defined( __APPLE__ ) && defined( NOVA_SYNC_HAS_EXPECTED )
