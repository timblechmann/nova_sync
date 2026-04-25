// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file event/support/boost_asio_support.hpp
///
/// Asynchronous event-wait helpers built on Boost.ASIO.
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
/// - **Cancellation / error** (`!result.has_value()`): `result.error()` holds the error code.
///
/// Example
/// -------
/// @code
///   nova::sync::native_manual_reset_event evt;
///   boost::asio::io_context ioc;
///
///   nova::sync::async_wait(ioc, evt,
///       [](auto result) {
///           if (result) {
///               // event fired
///           }
///       });
///
///   evt.signal();
///   ioc.run();
/// @endcode

#include <nova/sync/event/detail/async_support.hpp>

#if __has_include( <boost/asio.hpp> ) && defined( NOVA_SYNC_HAS_EXPECTED )

#    include <atomic>
#    include <future>
#    include <memory>
#    include <system_error>

#    include <boost/asio/dispatch.hpp>
#    include <boost/asio/io_context.hpp>

#    include <nova/sync/detail/syscall.hpp>
#    include <nova/sync/event/concepts.hpp>

#    if defined( __linux__ ) || defined( __APPLE__ )
#        include <boost/asio/posix/stream_descriptor.hpp>
#    elif defined( _WIN32 )
#        include <boost/asio/windows/object_handle.hpp>
#    endif

namespace nova::sync {

namespace detail {

#    if defined( __linux__ ) || defined( __APPLE__ )
using event_stream_descriptor = boost::asio::posix::stream_descriptor;

template < typename Handler >
auto async_wait_descriptor( boost::asio::posix::stream_descriptor& descriptor, Handler&& handler )
{
    return descriptor.async_wait( boost::asio::posix::stream_descriptor::wait_read, std::forward< Handler >( handler ) );
}
#    elif defined( _WIN32 )
using event_stream_descriptor = boost::asio::windows::object_handle;

template < typename Handler >
auto async_wait_descriptor( boost::asio::windows::object_handle& descriptor, Handler&& handler )
{
    return descriptor.async_wait( std::forward< Handler >( handler ) );
}
#    endif

// ---------------------------------------------------------------------------
// Non-cancellable async wait op
// ---------------------------------------------------------------------------

template < typename Event, typename Handler >
struct async_event_wait_op : std::enable_shared_from_this< async_event_wait_op< Event, Handler > >
{
    std::shared_ptr< event_stream_descriptor > sd_;
    Event&                                     evt_;
    Handler                                    handler_;

    async_event_wait_op( boost::asio::io_context& ioc, auto dup_handle, Event& evt, Handler&& handler ) :
        sd_( std::make_shared< event_stream_descriptor >( ioc, dup_handle ) ),
        evt_( evt ),
        handler_( std::forward< Handler >( handler ) )
    {}

    void start_wait()
    {
        async_wait_descriptor( *sd_, [ self = this->shared_from_this() ]( const boost::system::error_code& bec ) {
            if ( bec ) {
                // Descriptor error or cancellation
                return invoke_event_error( self->handler_, std::errc::operation_canceled );
            }

            if ( self->evt_.try_wait() ) {
                return invoke_event_success( self->handler_ );
            }

            // Spurious wakeup — retry
            self->start_wait();
        } );
    }

    void cancel()
    {
        sd_->cancel();
    }
};

// ---------------------------------------------------------------------------
// Cancellable async wait op
// ---------------------------------------------------------------------------

template < typename Event, typename Handler >
struct async_event_wait_cancellable_op :
    std::enable_shared_from_this< async_event_wait_cancellable_op< Event, Handler > >
{
    std::shared_ptr< event_stream_descriptor > sd_;
    Event&                                     evt_;
    Handler                                    handler_;
    std::atomic< bool >                        cancellation_requested_ { false };
    std::atomic< bool >                        handler_invoked_ { false };

    async_event_wait_cancellable_op( boost::asio::io_context& ioc, auto dup_handle, Event& evt, Handler&& handler ) :
        sd_( std::make_shared< event_stream_descriptor >( ioc, dup_handle ) ),
        evt_( evt ),
        handler_( std::forward< Handler >( handler ) )
    {}

    void cancel()
    {
        cancellation_requested_.store( true, std::memory_order_release );
        sd_->cancel();
    }

    void start_wait()
    {
        async_wait_descriptor( *sd_, [ self = this->shared_from_this() ]( const boost::system::error_code& bec ) {
            if ( self->is_cancelled() ) {
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_event_error( self->handler_, std::errc::operation_canceled );
                return;
            }

            if ( bec ) {
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_event_error( self->handler_, std::errc::operation_canceled );
                return;
            }

            if ( self->evt_.try_wait() ) {
                // Double-check cancellation after consuming the token
                if ( self->is_cancelled() ) {
                    // For auto_reset_event the token is already consumed; re-signal so it's not lost.
                    // For manual_reset_event try_wait() is non-destructive so nothing to restore.
                    // We cannot generically undo, so just deliver the success (the wait completed).
                }
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_event_success( self->handler_ );
                return;
            }

            // Spurious wakeup — retry
            self->start_wait();
        } );
    }

private:
    bool is_cancelled() const noexcept
    {
        return cancellation_requested_.load( std::memory_order_acquire );
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Public API — cancellable handle
// ---------------------------------------------------------------------------

/// @brief Opaque handle returned by `async_wait_cancellable`.
///
/// Exposes a `cancel()` method. Destroying the handle automatically cancels any
/// pending wait, delivering `errc::operation_canceled` to the handler.
template < typename Event, typename Handler >
struct boost_asio_event_wait_handle
{
    std::shared_ptr< detail::async_event_wait_cancellable_op< Event, Handler > > op_;

    boost_asio_event_wait_handle() = default;

    explicit boost_asio_event_wait_handle(
        std::shared_ptr< detail::async_event_wait_cancellable_op< Event, Handler > > op ) :
        op_( std::move( op ) )
    {}

    boost_asio_event_wait_handle( boost_asio_event_wait_handle&& ) noexcept            = default;
    boost_asio_event_wait_handle( const boost_asio_event_wait_handle& )                = delete;
    boost_asio_event_wait_handle& operator=( const boost_asio_event_wait_handle& )     = delete;
    boost_asio_event_wait_handle& operator=( boost_asio_event_wait_handle&& ) noexcept = default;

    ~boost_asio_event_wait_handle()
    {
        cancel();
    }

    /// @brief Cancel the pending async wait. Safe to call from any thread. Idempotent.
    void cancel()
    {
        if ( op_ ) {
            op_->cancel();
            op_.reset();
        }
    }

    explicit operator bool() const noexcept
    {
        return op_ != nullptr;
    }
};

// ---------------------------------------------------------------------------
// Public API — future-based state
// ---------------------------------------------------------------------------

/// @brief State returned by the future-based `async_wait` overload.
template < typename Event >
struct async_event_wait_future_state
{
    ~async_event_wait_future_state()
    {
        if ( descriptor )
            descriptor->cancel();
    }

    std::shared_ptr< detail::event_stream_descriptor > descriptor;
    std::future< void >                                future;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously waits for @p evt and invokes @p handler with an
///        `expected<void, error_code>` result.
///
/// The handler is called exactly once, from a thread running @p ioc, with
/// the signature:
///
/// @code
///   void handler(expected<void, std::error_code> result);
/// @endcode
///
/// - **Success** (`result.has_value()`): The event was signaled.
/// - **Cancellation / error** (`!result.has_value()`): `result.error()` holds the error.
///
/// @param ioc     Boost.ASIO io_context for async operations.
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param handler Callable invoked with `expected<void, std::error_code>`.
template < typename Event, typename Handler >
void async_wait( boost::asio::io_context& ioc, Event& evt, Handler&& handler )
    requires detail::invocable_with_void_expected< Handler > && concepts::native_async_event< Event >
{
    if ( evt.try_wait() ) {
        auto h = std::forward< Handler >( handler );
        boost::asio::dispatch( ioc, [ h = std::move( h ) ]() mutable {
            detail::invoke_event_success( h );
        } );
        return;
    }

    auto dup_handle = detail::duplicate_native_handle( evt.native_handle() );

    using Op = detail::async_event_wait_op< Event, std::decay_t< Handler > >;
    auto op  = std::make_shared< Op >( ioc, dup_handle, evt, std::forward< Handler >( handler ) );
    op->start_wait();
}

/// @brief Asynchronously waits for @p evt and returns a cancellable handle.
///
/// Identical to the non-cancellable `async_wait` overload except it returns a
/// `boost_asio_event_wait_handle` exposing a `cancel()` method. Destroying
/// the handle automatically cancels any pending wait.
///
/// @param ioc     Boost.ASIO io_context for async operations.
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param handler Callable invoked with `expected<void, std::error_code>`.
/// @return Handle allowing cancellation of the pending wait.
template < typename Event, typename Handler >
auto async_wait_cancellable( boost::asio::io_context& ioc, Event& evt, Handler&& handler )
    -> boost_asio_event_wait_handle< Event, std::decay_t< Handler > >
    requires detail::invocable_with_void_expected< Handler > && concepts::native_async_event< Event >
{
    using Op = detail::async_event_wait_cancellable_op< Event, std::decay_t< Handler > >;

    if ( evt.try_wait() ) {
        boost::asio::dispatch( ioc, [ handler = std::forward< Handler >( handler ) ]() mutable {
            detail::invoke_event_success( handler );
        } );
        return boost_asio_event_wait_handle< Event, std::decay_t< Handler > > {};
    }

    auto dup_handle = detail::duplicate_native_handle( evt.native_handle() );
    auto op         = std::make_shared< Op >( ioc, dup_handle, evt, std::forward< Handler >( handler ) );
    op->start_wait();
    return boost_asio_event_wait_handle< Event, std::decay_t< Handler > > { op };
}

/// @brief Asynchronously waits for @p evt and returns a future.
///
/// The returned future becomes ready (with no value) when the event fires.
/// If the event is already signaled the future is immediately ready.
///
/// @param ioc Boost.ASIO io_context for async operations.
/// @param evt An event satisfying `native_async_event` with `native_handle()`.
template < typename Event >
async_event_wait_future_state< Event > async_wait( boost::asio::io_context& ioc, Event& evt )
    requires concepts::native_async_event< Event >
{
    using promise_t = std::promise< void >;

    if ( evt.try_wait() ) {
        promise_t promise;
        promise.set_value();
        return async_event_wait_future_state< Event > { {}, promise.get_future() };
    }

    auto dup_handle = detail::duplicate_native_handle( evt.native_handle() );
    auto sd         = std::make_shared< detail::event_stream_descriptor >( ioc, dup_handle );

#    ifdef __cpp_lib_move_only_function
    auto promise = promise_t();
    auto future  = promise.get_future();

    auto do_wait = std::make_shared< std::move_only_function< void() > >();
    *do_wait     = [ &evt, sd, promise = std::move( promise ), do_wait ]() mutable {
        detail::async_wait_descriptor(
            *sd, [ &evt, sd, promise = std::move( promise ), do_wait ]( const boost::system::error_code& ec ) mutable {
            if ( ec ) {
                *do_wait = nullptr;
                return; // cancelled — promise intentionally left unfulfilled
            }

            if ( evt.try_wait() ) {
                promise.set_value();
                *do_wait = nullptr;
            } else {
                std::invoke( *do_wait ); // spurious — retry
            }
        } );
    };
#    else
    auto promise = std::make_shared< promise_t >();
    auto future  = promise->get_future();

    auto do_wait = std::make_shared< std::function< void() > >();
    *do_wait     = [ &evt, sd, promise, do_wait ]() mutable {
        detail::async_wait_descriptor( *sd,
                                       [ &evt, sd, promise, do_wait ]( const boost::system::error_code& ec ) mutable {
            if ( ec ) {
                *do_wait = nullptr;
                return;
            }

            if ( evt.try_wait() ) {
                promise->set_value();
                *do_wait = nullptr;
            } else {
                std::invoke( *do_wait );
            }
        } );
    };
#    endif

    std::invoke( *do_wait );

    return async_event_wait_future_state< Event > { std::move( sd ), std::move( future ) };
}

} // namespace nova::sync

#endif // __has_include( <boost/asio.hpp> ) && defined( NOVA_SYNC_HAS_EXPECTED )
