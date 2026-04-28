// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file semaphore/support/libdispatch_support.hpp
///
/// Asynchronous semaphore acquire helpers built on Apple libdispatch
/// (Grand Central Dispatch).
///
/// Handler signature
/// -----------------
/// @code
///   void handler(expected<void, std::error_code> result);
/// @endcode
///
/// - **Success** (`result.has_value()`): A token was acquired.
/// - **Cancellation / error** (`!result.has_value()`): `result.error()` contains the error code.
///
/// Example
/// -------
/// @code
///   nova::sync::native_async_semaphore sem(0);
///   dispatch_queue_t queue = dispatch_queue_create("com.example.q", DISPATCH_QUEUE_SERIAL);
///
///   nova::sync::async_acquire(sem, queue,
///       [](auto result) {
///           if (result) {
///               // token acquired
///           }
///       });
///
///   sem.release();
/// @endcode

#include <nova/sync/semaphore/detail/async_support.hpp>

#if defined( __APPLE__ ) && defined( NOVA_SYNC_HAS_EXPECTED )

#  include <atomic>
#  include <future>
#  include <memory>
#  include <system_error>

#  include <dispatch/dispatch.h>

#  include <nova/sync/semaphore/concepts.hpp>

namespace nova::sync {

namespace detail {

#  if !__has_feature( objc_arc )
inline void release_semaphore_dispatch_object( dispatch_object_t obj ) noexcept
{
    if ( obj )
        dispatch_release( obj );
}
#  else
inline void release_semaphore_dispatch_object( dispatch_object_t obj ) noexcept
{}
#  endif

// ---------------------------------------------------------------------------
// Cancellation state
// ---------------------------------------------------------------------------

struct dispatch_semaphore_cancel_state
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
// Non-cancellable async acquire state
// ---------------------------------------------------------------------------

template < typename Semaphore, typename Handler >
struct dispatch_semaphore_acquire_state :
    std::enable_shared_from_this< dispatch_semaphore_acquire_state< Semaphore, Handler > >
{
    Semaphore&        sem_;
    dispatch_queue_t  queue_;
    Handler           handler_;
    dispatch_source_t src_ {};

    dispatch_semaphore_acquire_state( Semaphore& sem, dispatch_queue_t queue, Handler&& handler ) :
        sem_( sem ),
        queue_( queue ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~dispatch_semaphore_acquire_state()
    {
        if ( src_ )
            release_semaphore_dispatch_object( src_ );
    }

    void start()
    {
        if ( sem_.try_acquire() ) {
            dispatch_async( queue_, [ self = this->shared_from_this() ]() {
                invoke_semaphore_success( self->handler_ );
            } );
            return;
        }

        src_ = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ, uintptr_t( sem_.native_handle() ), 0, queue_ );

        dispatch_source_set_event_handler( src_, [ self = this->shared_from_this() ]() {
            if ( self->sem_.try_acquire() ) {
                dispatch_source_cancel( self->src_ );
                dispatch_async( self->queue_, [ self ]() {
                    invoke_semaphore_success( self->handler_ );
                } );
            }
            // Otherwise spurious — source stays active
        } );

        dispatch_source_set_cancel_handler( src_, [ self = this->shared_from_this() ]() {
            if ( self->src_ ) {
                release_semaphore_dispatch_object( self->src_ );
                self->src_ = nullptr;
            }
        } );

        dispatch_resume( src_ );
    }
};

// ---------------------------------------------------------------------------
// Cancellable async acquire state
// ---------------------------------------------------------------------------

template < typename Semaphore, typename Handler >
struct dispatch_semaphore_acquire_cancellable_state :
    std::enable_shared_from_this< dispatch_semaphore_acquire_cancellable_state< Semaphore, Handler > >
{
    Semaphore&                                               sem_;
    dispatch_queue_t                                         queue_;
    Handler                                                  handler_;
    dispatch_source_t                                        src_ {};
    const std::shared_ptr< dispatch_semaphore_cancel_state > cancel_state_;

    dispatch_semaphore_acquire_cancellable_state( Semaphore& sem, dispatch_queue_t queue, Handler&& handler ) :
        sem_( sem ),
        queue_( queue ),
        handler_( std::forward< Handler >( handler ) ),
        cancel_state_( std::make_shared< dispatch_semaphore_cancel_state >() )
    {}

    ~dispatch_semaphore_acquire_cancellable_state()
    {
        if ( src_ )
            release_semaphore_dispatch_object( src_ );
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
                    invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
                } );
            return;
        }

        if ( sem_.try_acquire() ) {
            if ( cancel_state_->is_cancelled() ) {
                // We acquired a token but got cancelled — release it back
                sem_.release();
                if ( cancel_state_->try_mark_invoked() )
                    dispatch_async( queue_, [ self = this->shared_from_this() ] {
                        invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
                    } );
                return;
            }

            if ( cancel_state_->try_mark_invoked() )
                dispatch_async( queue_, [ self = this->shared_from_this() ] {
                    invoke_semaphore_success( self->handler_ );
                } );
            return;
        }

        src_ = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ, uintptr_t( sem_.native_handle() ), 0, queue_ );

        dispatch_source_set_event_handler( src_, [ self = this->shared_from_this() ] {
            if ( self->cancel_state_->is_cancelled() ) {
                if ( self->cancel_state_->try_mark_invoked() ) {
                    dispatch_source_cancel( self->src_ );
                    dispatch_async( self->queue_, [ self ] {
                        invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
                    } );
                }
                return;
            }

            if ( self->sem_.try_acquire() ) {
                if ( self->cancel_state_->try_mark_invoked() ) {
                    dispatch_source_cancel( self->src_ );
                    dispatch_async( self->queue_, [ self ]() {
                        invoke_semaphore_success( self->handler_ );
                    } );
                }
            }
            // Spurious — source stays active
        } );

        dispatch_source_set_cancel_handler( src_, [ self = this->shared_from_this() ] {
            if ( self->src_ ) {
                release_semaphore_dispatch_object( self->src_ );
                self->src_ = nullptr;
            }
            if ( self->cancel_state_->is_cancelled() && self->cancel_state_->try_mark_invoked() ) {
                dispatch_async( self->queue_, [ self ]() {
                    invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
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

class dispatch_semaphore_acquire_handle
{
public:
    dispatch_semaphore_acquire_handle() = default;

    explicit dispatch_semaphore_acquire_handle( std::shared_ptr< detail::dispatch_semaphore_cancel_state > s ) :
        state_( std::move( s ) )
    {}

    dispatch_semaphore_acquire_handle( dispatch_semaphore_acquire_handle&& other ) noexcept :
        state_( std::move( other.state_ ) )
    {}

    dispatch_semaphore_acquire_handle& operator=( dispatch_semaphore_acquire_handle&& other ) noexcept
    {
        if ( this != &other ) {
            cancel();
            state_ = std::move( other.state_ );
        }
        return *this;
    }

    dispatch_semaphore_acquire_handle( const dispatch_semaphore_acquire_handle& )            = delete;
    dispatch_semaphore_acquire_handle& operator=( const dispatch_semaphore_acquire_handle& ) = delete;

    ~dispatch_semaphore_acquire_handle()
    {
        cancel();
    }

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
    std::shared_ptr< detail::dispatch_semaphore_cancel_state > state_;
};

// ---------------------------------------------------------------------------
// Public API — future-based state
// ---------------------------------------------------------------------------

template < typename Semaphore >
struct dispatch_semaphore_acquire_future_state
{
    std::future< void > future;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously acquires a token from @p sem on @p queue and invokes @p handler.
template < typename Semaphore, typename Handler >
void async_acquire( Semaphore& sem, dispatch_queue_t queue, Handler&& handler )
    requires detail::invocable_with_semaphore_expected< Handler > && concepts::native_async_semaphore< Semaphore >
{
    using state_type = detail::dispatch_semaphore_acquire_state< Semaphore, std::decay_t< Handler > >;
    auto state       = std::make_shared< state_type >( sem, queue, std::forward< Handler >( handler ) );
    state->start();
}

/// @brief Asynchronously acquires a token from @p sem on @p queue with cancellation support.
template < typename Semaphore, typename Handler >
dispatch_semaphore_acquire_handle async_acquire_cancellable( Semaphore& sem, dispatch_queue_t queue, Handler&& handler )
    requires detail::invocable_with_semaphore_expected< Handler > && concepts::native_async_semaphore< Semaphore >
{
    using state_type = detail::dispatch_semaphore_acquire_cancellable_state< Semaphore, std::decay_t< Handler > >;
    auto op          = std::make_shared< state_type >( sem, queue, std::forward< Handler >( handler ) );
    op->start();
    return dispatch_semaphore_acquire_handle { op->cancel_state_ };
}

/// @brief Asynchronously acquires a token from @p sem on @p queue and returns a future.
template < typename Semaphore >
dispatch_semaphore_acquire_future_state< Semaphore > async_acquire( Semaphore& sem, dispatch_queue_t queue )
    requires concepts::native_async_semaphore< Semaphore >
{
    using promise_t = std::promise< void >;

    if ( sem.try_acquire() ) {
        promise_t promise;
        promise.set_value();
        return dispatch_semaphore_acquire_future_state< Semaphore > { promise.get_future() };
    }

    auto promise    = std::make_shared< promise_t >();
    auto future     = promise->get_future();
    auto retry_func = std::make_shared< std::function< void() > >();

    *retry_func = [ &sem, promise, retry_func, queue ]() {
        dispatch_source_t src
            = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ, uintptr_t( sem.native_handle() ), 0, queue );

        dispatch_source_set_event_handler( src, [ &sem, promise, retry_func, src, queue ] {
            if ( sem.try_acquire() ) {
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
            detail::release_semaphore_dispatch_object( src );
        } );

        dispatch_resume( src );
    };

    dispatch_async( queue, [ retry_func ] {
        ( *retry_func )();
    } );

    return dispatch_semaphore_acquire_future_state< Semaphore > { std::move( future ) };
}

} // namespace nova::sync

#endif // defined( __APPLE__ ) && defined( NOVA_SYNC_HAS_EXPECTED )
