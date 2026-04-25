// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file event/support/libdispatch_support.hpp
///
/// Asynchronous event-wait helpers built on Apple libdispatch
/// (Grand Central Dispatch).
///
/// Handler signature
/// -----------------
/// All callbacks follow the same convention:
///
/// @code
///   void handler(expected<void, std::error_code> result);
/// @endcode
///
/// where `expected` is `std::expected` (C++23) or `tl::expected` (C++20+).
///
/// - **Success** (`result.has_value()`): The event fired.
/// - **Cancellation / error** (`!result.has_value()`): `result.error()` contains the error code.
///
/// Example
/// -------
/// @code
///   nova::sync::native_manual_reset_event evt;
///   dispatch_queue_t queue = dispatch_queue_create("com.example.q", DISPATCH_QUEUE_SERIAL);
///
///   nova::sync::async_wait(evt, queue,
///       [](auto result) {
///           if (result) {
///               // event fired
///           }
///       });
///
///   evt.signal();
/// @endcode

#include <nova/sync/event/detail/async_support.hpp>

#if defined( __APPLE__ ) && defined( NOVA_SYNC_HAS_EXPECTED )

#  include <atomic>
#  include <future>
#  include <memory>
#  include <system_error>

#  include <dispatch/dispatch.h>

#  include <nova/sync/event/concepts.hpp>

namespace nova::sync {

namespace detail {

#  if !__has_feature( objc_arc )
inline void release_event_dispatch_object( dispatch_object_t obj ) noexcept
{
    if ( obj )
        dispatch_release( obj );
}
#  else
inline void release_event_dispatch_object( dispatch_object_t obj ) noexcept
{}
#  endif

// ---------------------------------------------------------------------------
// Cancellation state
// ---------------------------------------------------------------------------

struct dispatch_event_cancel_state
{
    std::atomic< bool >     cancelled { false };
    std::atomic< bool >     handler_invoked { false };
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

// ---------------------------------------------------------------------------
// Non-cancellable async wait state
// ---------------------------------------------------------------------------

template < typename Event, typename Handler >
struct dispatch_event_wait_state : std::enable_shared_from_this< dispatch_event_wait_state< Event, Handler > >
{
    Event&            evt_;
    dispatch_queue_t  queue_;
    Handler           handler_;
    dispatch_source_t src_ {};

    dispatch_event_wait_state( Event& evt, dispatch_queue_t queue, Handler&& handler ) :
        evt_( evt ),
        queue_( queue ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~dispatch_event_wait_state()
    {
        if ( src_ )
            release_event_dispatch_object( src_ );
    }

    void start()
    {
        if ( evt_.try_wait() ) {
            dispatch_async( queue_, [ self = this->shared_from_this() ]() {
                invoke_event_success( self->handler_ );
            } );
            return;
        }

        src_ = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ,
                                       static_cast< uintptr_t >( evt_.native_handle() ),
                                       0,
                                       queue_ );

        dispatch_source_set_event_handler( src_, [ self = this->shared_from_this() ]() {
            if ( self->evt_.try_wait() ) {
                dispatch_source_cancel( self->src_ );
                dispatch_async( self->queue_, [ self ]() {
                    invoke_event_success( self->handler_ );
                } );
            }
            // Otherwise spurious — source stays active, wait for next event
        } );

        dispatch_source_set_cancel_handler( src_, [ self = this->shared_from_this() ]() {
            if ( self->src_ ) {
                release_event_dispatch_object( self->src_ );
                self->src_ = nullptr;
            }
        } );

        dispatch_resume( src_ );
    }
};

// ---------------------------------------------------------------------------
// Cancellable async wait state
// ---------------------------------------------------------------------------

template < typename Event, typename Handler >
struct dispatch_event_wait_cancellable_state :
    std::enable_shared_from_this< dispatch_event_wait_cancellable_state< Event, Handler > >
{
    Event&                                               evt_;
    dispatch_queue_t                                     queue_;
    Handler                                              handler_;
    dispatch_source_t                                    src_ {};
    const std::shared_ptr< dispatch_event_cancel_state > cancel_state_;

    dispatch_event_wait_cancellable_state( Event& evt, dispatch_queue_t queue, Handler&& handler ) :
        evt_( evt ),
        queue_( queue ),
        handler_( std::forward< Handler >( handler ) ),
        cancel_state_( std::make_shared< dispatch_event_cancel_state >() )
    {}

    ~dispatch_event_wait_cancellable_state()
    {
        if ( src_ )
            release_event_dispatch_object( src_ );
    }

    void start()
    {
        cancel_state_->cancellation_callback_ = [ self = this->shared_from_this() ] {
            if ( self->src_ )
                dispatch_source_cancel( self->src_ );
        };

        if ( cancel_state_->is_cancelled() ) {
            if ( cancel_state_->try_mark_invoked() )
                dispatch_async( queue_, [ self = this->shared_from_this() ] {
                    invoke_event_error( self->handler_, std::errc::operation_canceled );
                } );
            return;
        }

        if ( evt_.try_wait() ) {
            if ( cancel_state_->is_cancelled() ) {
                if ( cancel_state_->try_mark_invoked() )
                    dispatch_async( queue_, [ self = this->shared_from_this() ] {
                        invoke_event_error( self->handler_, std::errc::operation_canceled );
                    } );
                return;
            }

            if ( cancel_state_->try_mark_invoked() )
                dispatch_async( queue_, [ self = this->shared_from_this() ] {
                    invoke_event_success( self->handler_ );
                } );
            return;
        }

        src_ = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ,
                                       static_cast< uintptr_t >( evt_.native_handle() ),
                                       0,
                                       queue_ );

        dispatch_source_set_event_handler( src_, [ self = this->shared_from_this() ] {
            if ( self->cancel_state_->is_cancelled() ) {
                if ( self->cancel_state_->try_mark_invoked() ) {
                    dispatch_source_cancel( self->src_ );
                    dispatch_async( self->queue_, [ self ] {
                        invoke_event_error( self->handler_, std::errc::operation_canceled );
                    } );
                }
                return;
            }

            if ( self->evt_.try_wait() ) {
                if ( self->cancel_state_->try_mark_invoked() ) {
                    dispatch_source_cancel( self->src_ );
                    dispatch_async( self->queue_, [ self ]() {
                        invoke_event_success( self->handler_ );
                    } );
                }
            }
            // Spurious — source stays active
        } );

        dispatch_source_set_cancel_handler( src_, [ self = this->shared_from_this() ] {
            if ( self->src_ ) {
                release_event_dispatch_object( self->src_ );
                self->src_ = nullptr;
            }
            if ( self->cancel_state_->is_cancelled() && self->cancel_state_->try_mark_invoked() ) {
                dispatch_async( self->queue_, [ self ]() {
                    invoke_event_error( self->handler_, std::errc::operation_canceled );
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

/// @brief Opaque handle returned by `async_wait_cancellable`.
///
/// Exposes a `cancel()` method. Destroying the handle automatically cancels
/// any pending wait, delivering `errc::operation_canceled` to the handler.
///
/// Thread safety: `cancel()` and the destructor are safe to call from any thread.
class dispatch_event_wait_handle
{
public:
    dispatch_event_wait_handle() = default;

    explicit dispatch_event_wait_handle( std::shared_ptr< detail::dispatch_event_cancel_state > s ) :
        state_( std::move( s ) )
    {}

    dispatch_event_wait_handle( dispatch_event_wait_handle&& other ) noexcept :
        state_( std::move( other.state_ ) )
    {}

    dispatch_event_wait_handle& operator=( dispatch_event_wait_handle&& other ) noexcept
    {
        if ( this != &other ) {
            cancel();
            state_ = std::move( other.state_ );
        }
        return *this;
    }

    dispatch_event_wait_handle( const dispatch_event_wait_handle& )            = delete;
    dispatch_event_wait_handle& operator=( const dispatch_event_wait_handle& ) = delete;

    ~dispatch_event_wait_handle()
    {
        cancel();
    }

    /// @brief Cancel the pending wait. Safe to call from any thread. Idempotent.
    void cancel()
    {
        if ( state_ )
            state_->cancel();
    }

    explicit operator bool() const noexcept
    {
        return state_ != nullptr;
    }

private:
    std::shared_ptr< detail::dispatch_event_cancel_state > state_;
};

// ---------------------------------------------------------------------------
// Public API — future-based state
// ---------------------------------------------------------------------------

/// @brief State returned by the future-based `async_wait` overload.
template < typename Event >
struct dispatch_event_wait_future_state
{
    std::future< void > future;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously waits for @p evt on @p queue and invokes @p handler
///        with an `expected<void, error_code>` result.
///
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param queue   Dispatch queue for event delivery and @p handler invocation.
/// @param handler Callable invoked with `expected<void, std::error_code>`.
template < typename Event, typename Handler >
void async_wait( Event& evt, dispatch_queue_t queue, Handler&& handler )
    requires detail::invocable_with_void_expected< Handler > && concepts::native_async_event< Event >
{
    using state_type = detail::dispatch_event_wait_state< Event, std::decay_t< Handler > >;
    auto state       = std::make_shared< state_type >( evt, queue, std::forward< Handler >( handler ) );
    state->start();
}

/// @brief Asynchronously waits for @p evt on @p queue and returns a cancellable handle.
///
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param queue   Dispatch queue for event delivery and @p handler invocation.
/// @param handler Callable invoked with `expected<void, std::error_code>`.
/// @return Handle allowing cancellation of the pending wait.
template < typename Event, typename Handler >
dispatch_event_wait_handle async_wait_cancellable( Event& evt, dispatch_queue_t queue, Handler&& handler )
    requires detail::invocable_with_void_expected< Handler > && concepts::native_async_event< Event >
{
    using state_type = detail::dispatch_event_wait_cancellable_state< Event, std::decay_t< Handler > >;
    auto op          = std::make_shared< state_type >( evt, queue, std::forward< Handler >( handler ) );
    op->start();
    return dispatch_event_wait_handle { op->cancel_state_ };
}

/// @brief Asynchronously waits for @p evt on @p queue and returns a future.
///
/// The returned future becomes ready (with no value) when the event fires.
///
/// @param evt   An event satisfying `native_async_event` with `native_handle()`.
/// @param queue Dispatch queue for event delivery.
template < typename Event >
dispatch_event_wait_future_state< Event > async_wait( Event& evt, dispatch_queue_t queue )
    requires concepts::native_async_event< Event >
{
    using promise_t = std::promise< void >;

    if ( evt.try_wait() ) {
        promise_t promise;
        promise.set_value();
        return dispatch_event_wait_future_state< Event > { promise.get_future() };
    }

    auto promise    = std::make_shared< promise_t >();
    auto future     = promise->get_future();
    auto retry_func = std::make_shared< std::function< void() > >();

    *retry_func = [ &evt, promise, retry_func, queue ]() {
        dispatch_source_t src = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ,
                                                        static_cast< uintptr_t >( evt.native_handle() ),
                                                        0,
                                                        queue );

        dispatch_source_set_event_handler( src, [ &evt, promise, retry_func, src, queue ] {
            if ( evt.try_wait() ) {
                dispatch_source_cancel( src );
                dispatch_async( dispatch_get_global_queue( QOS_CLASS_DEFAULT, 0 ), [ promise ] {
                    promise->set_value();
                } );
            } else {
                // Spurious — retry
                dispatch_source_cancel( src );
                dispatch_async( queue, [ retry_func ] {
                    ( *retry_func )();
                } );
            }
        } );

        dispatch_source_set_cancel_handler( src, [ src ] {
            detail::release_event_dispatch_object( src );
        } );

        dispatch_resume( src );
    };

    dispatch_async( queue, [ retry_func ] {
        ( *retry_func )();
    } );

    return dispatch_event_wait_future_state< Event > { std::move( future ) };
}

} // namespace nova::sync

#endif // defined( __APPLE__ ) && defined( NOVA_SYNC_HAS_EXPECTED )
