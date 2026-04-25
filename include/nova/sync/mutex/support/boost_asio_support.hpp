// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

/// @file boost_asio_support.hpp
/// Asynchronous mutex acquisition helpers built on Boost.ASIO.

#pragma once

#include <nova/sync/mutex/annotations.hpp>
#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/mutex/detail/async_support.hpp>

#if __has_include( <boost/asio.hpp>) && defined( NOVA_SYNC_HAS_EXPECTED )

#    include <future>
#    include <memory>
#    include <mutex>
#    include <system_error>

#    include <boost/asio/dispatch.hpp>
#    include <boost/asio/io_context.hpp>

#    include <nova/sync/detail/syscall.hpp>
#    include <nova/sync/mutex/detail/async_support.hpp>

#    if defined( __linux__ ) || defined( __APPLE__ )
#        include <boost/asio/posix/stream_descriptor.hpp>
#    elif defined( _WIN32 )
#        include <boost/asio/windows/object_handle.hpp>
#    endif

namespace nova::sync {


namespace detail {

#    if defined( __linux__ ) || defined( __APPLE__ )
using native_stream_descriptor = boost::asio::posix::stream_descriptor;
#    elif defined( _WIN32 )
using native_stream_descriptor = boost::asio::windows::object_handle;
#    endif

#    if defined( __linux__ ) || defined( __APPLE__ )
template < typename Handler >
auto async_wait( boost::asio::posix::stream_descriptor& descriptor, Handler&& handler )
{
    return descriptor.async_wait( boost::asio::posix::stream_descriptor::wait_read, std::forward< Handler >( handler ) );
}
#    elif defined( _WIN32 )
auto async_wait( boost::asio::windows::object_handle& descriptor, Handler&& handler )
{
    return descriptor.async_wait( std::forward< Handler >( handler ) );
}
#    endif

template < typename Mutex, typename Handler >
struct async_acquire_op : std::enable_shared_from_this< async_acquire_op< Mutex, Handler > >
{
    std::shared_ptr< native_stream_descriptor > sd_;
    Mutex&                                      mtx_;
    Handler                                     handler_;
    std::atomic< bool >                         cancellation_requested_ { false };
    detail::async_waiter_guard< Mutex >         waiter_guard_;

    async_acquire_op( boost::asio::io_context& ioc, auto dup_fd, Mutex& mtx, Handler&& handler ) :
        sd_( std::make_shared< native_stream_descriptor >( ioc, dup_fd ) ),
        mtx_( mtx ),
        handler_( std::forward< Handler >( handler ) ),
        waiter_guard_( mtx )
    {}

    void cancel()
    {
        cancellation_requested_.store( true, std::memory_order_release );
        sd_->cancel();
    }

    void start_wait()
    {
        async_wait( *sd_, [ self = this->shared_from_this() ]( const boost::system::error_code& bec ) {
            // Check if cancellation was requested before handler execution
            // If so, invoke handler with cancellation error instead
            if ( self->is_cancelled() ) {
                self->waiter_guard_.release();
                return invoke_with_error< Mutex >( self->handler_,
                                                   std::make_error_code( std::errc::operation_canceled ) );
            }

            std::error_code ec = std::make_error_code( static_cast< std::errc >( bec.value() ) );
            if ( ec ) {
                self->waiter_guard_.release();
                // Cancelled or descriptor error — forward as operation_canceled.
                return invoke_with_error< Mutex >( self->handler_,
                                                   std::make_error_code( std::errc::operation_canceled ) );
            }

            if ( self->waiter_guard_.try_acquire() ) {
                return invoke_with_lock( self->handler_, std::unique_lock( self->mtx_, std::adopt_lock ) );
            }

            self->start_wait(); // spurious wakeup — retry
        } );
    }

private:
    bool is_cancelled() const noexcept
    {
        return cancellation_requested_.load( std::memory_order_acquire );
    }
};


template < typename Mutex, typename Handler >
void start_async_acquire( boost::asio::io_context& ioc, Mutex& mtx, Handler&& handler )
{}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// @brief Opaque handle returned by `async_acquire_cancellable`.
///
/// Keeps the in-flight operation alive and exposes a `cancel()` method.
/// Destroying the handle (either by going out of scope or explicit destruction)
/// automatically cancels any pending wait and causes the handler to be called
/// with `errc::operation_canceled`.
template < typename Mutex, typename Handler >
struct boost_asio_acquire_handle
{
    std::shared_ptr< detail::async_acquire_op< Mutex, Handler > > op_;

    boost_asio_acquire_handle() = default;

    explicit boost_asio_acquire_handle( std::shared_ptr< detail::async_acquire_op< Mutex, Handler > > op ) :
        op_( std::move( op ) )
    {}

    // Move semantics
    boost_asio_acquire_handle( boost_asio_acquire_handle&& ) noexcept = default;
    boost_asio_acquire_handle& operator=( boost_asio_acquire_handle&& other ) noexcept
    {
        cancel(); // Auto-cancel before replacing
        op_ = std::move( other.op_ );
        return *this;
    }

    boost_asio_acquire_handle( const boost_asio_acquire_handle& )            = delete;
    boost_asio_acquire_handle& operator=( const boost_asio_acquire_handle& ) = delete;

    // Destructor: auto-cancel
    ~boost_asio_acquire_handle()
    {
        cancel();
    }

    /// @brief Cancel the pending async operation.
    ///
    /// If a wait is in progress, the stream descriptor is cancelled and the
    /// handler is invoked with `errc::operation_canceled`. If the operation has
    /// already completed, this is a no-op.
    void cancel()
    {
        if ( op_ ) {
            op_->cancel();
            op_.reset();
        }
    }
};

/// @brief Asynchronously acquires @p mtx and returns a future-based state.
///
/// If the mutex is immediately available (`try_lock()` succeeds), the
/// returned future is already ready and holds a locked `unique_lock`.
/// Otherwise the acquisition is performed asynchronously on @p ioc; once the
/// mutex is taken the promise is fulfilled with a `unique_lock` that owns it.
///
/// Handler / future value
/// ----------------------
/// The future carries `std::unique_lock<Mutex>` with `owns_lock() == true`.
/// The caller is responsible for unlocking — either explicitly via
/// `lock.unlock()` or by letting the `unique_lock` destruct.
///
/// Lifetime
/// --------
/// - @p ioc must remain live until the operation completes or is cancelled.
/// - @p mtx must remain live until the operation completes or is cancelled.
///   Destroying the mutex while a wait is pending is **undefined behaviour**.
/// - `async_acquire_future_state::descriptor` keeps the internal operation
///   alive.  Do not destroy it before the future is ready unless cancelling.
///
/// Cancellation
/// ------------
/// Call `state.descriptor->cancel()` to abort a pending wait.  After
/// cancellation the future will never become ready.  Do not call
/// `state.future.get()` after cancellation (or catch `std::future_error`).
///
/// Example
/// -------
/// @code
///   nova::sync::native_async_mutex mtx;
///   asio_runner runner; // io_context on a background thread
///
///   mtx.lock(); // hold the lock from the main thread
///
///   auto [descriptor, fut] = nova::sync::async_acquire(runner.ioc, mtx);
///
///   mtx.unlock(); // release: the async waiter will now acquire
///
///   auto lock = fut.get(); // blocks until acquired; lock.owns_lock() == true
///   // ... critical section ...
///   // lock released automatically on scope exit
/// @endcode
template < typename Mutex >
struct async_acquire_future_state
{
    ~async_acquire_future_state()
    {
        if ( descriptor )
            descriptor->cancel(); // Cancel any pending wait on destruction
    }

    std::shared_ptr< detail::native_stream_descriptor > descriptor;
    std::future< std::unique_lock< Mutex > >            future;
};

template < typename Mutex >
async_acquire_future_state< Mutex > async_acquire( boost::asio::io_context& ioc, Mutex& mtx )
    requires concepts::native_async_mutex< Mutex >
NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
{
    using promise_t = std::promise< std::unique_lock< Mutex > >;

    if ( mtx.try_lock() ) {
        promise_t promise;
        promise.set_value( std::unique_lock( mtx, std::adopt_lock ) );
        return async_acquire_future_state {
            {},
            promise.get_future(),
        };
    }

    // Register as a waiter so unlock() will trigger the async event (fast-path types only)
    auto dup_fd = detail::duplicate_native_handle( mtx.native_handle() );
    auto sd     = std::make_shared< detail::native_stream_descriptor >( ioc, dup_fd );

#    ifdef __cpp_lib_move_only_function
    auto promise = promise_t();
    auto future  = promise.get_future();

    auto do_wait = std::make_shared< std::move_only_function< void() > >();
    auto wg      = std::make_shared< detail::async_waiter_guard< Mutex > >( mtx );
    *do_wait     = [ &, sd, promise = std::move( promise ), do_wait, wg ]() mutable {
        detail::async_wait(
            *sd, [ &, sd, promise = std::move( promise ), do_wait, wg ]( const boost::system::error_code& ec ) mutable {
            if ( ec ) {
                wg->release();
                *do_wait = nullptr; // break the self-referential cycle
                return;             // cancelled — promise intentionally left unfulfilled
            }

            if ( wg->try_acquire() ) {
                promise.set_value( std::unique_lock< Mutex >( mtx, std::adopt_lock ) );
                *do_wait = nullptr;      // break the self-referential cycle
            } else {
                std::invoke( *do_wait ); // spurious wakeup — retry
            }
        } );
    };
#    else
    auto promise = std::make_shared< promise_t >();
    auto future  = promise->get_future();

    auto do_wait = std::make_shared< std::function< void() > >();
    auto wg      = std::make_shared< detail::async_waiter_guard< Mutex > >( mtx );
    *do_wait     = [ &, sd, promise = std::move( promise ), do_wait, wg ]() mutable {
        detail::async_wait(
            *sd, [ &, sd, promise = std::move( promise ), do_wait, wg ]( const boost::system::error_code& ec ) mutable {
            if ( ec ) {
                wg->release();
                *do_wait = nullptr; // break the self-referential cycle
                return;             // cancelled — promise intentionally left unfulfilled
            }

            if ( wg->try_acquire() ) {
                promise->set_value( std::unique_lock( mtx, std::adopt_lock ) );
                *do_wait = nullptr;      // break the self-referential cycle
            } else {
                std::invoke( *do_wait ); // spurious wakeup — retry
            }
        } );
    };
#    endif

    std::invoke( *do_wait );

    return async_acquire_future_state {
        std::move( sd ),
        std::move( future ),
    };
}

/// @brief Asynchronously acquires @p mtx and invokes @p handler with an
///        `expected<unique_lock, error_code>` result.
///
/// The handler is called exactly once, from a thread running @p ioc, with
/// the signature:
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
/// If the mutex is immediately available (`try_lock()` succeeds), @p handler
/// is dispatched to @p ioc via `boost::asio::dispatch` — it fires
/// asynchronously on the next `ioc.run()` iteration.
///
/// @param ioc     Boost.ASIO io_context for async operations.
/// @param mtx     A mutex satisfying `native_async_mutex` with `native_handle()`.
/// @param handler Callable invoked with `expected<std::unique_lock<Mutex>, std::error_code>`.
///
/// Lifetime
/// --------
/// - @p ioc must remain live until @p handler fires.
/// - @p mtx must remain live until @p handler fires.  Destroying the mutex
///   while a wait is pending is **undefined behaviour**.
/// - The operation object is reference-counted internally; no lifetime
///   management is required unless cancellation is needed.
///
/// Cancellation
/// ------------
/// Use `async_acquire_cancellable` (the overload returning
/// `boost_asio_acquire_handle`) if you need to cancel a pending wait.
///
/// Example
/// -------
/// @code
///   nova::sync::native_async_mutex mtx;
///   asio_runner runner;
///
///   nova::sync::async_acquire(runner.ioc, mtx,
///       [](auto result) {
///           if (result) {
///               auto& lock = *result;
///               // lock.owns_lock() == true — critical section here
///           } else {
///               // result.error() == errc::operation_canceled (or other error)
///           }
///       });
/// @endcode
template < typename Mutex, typename Handler >
NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS void async_acquire( boost::asio::io_context& ioc, Mutex& mtx, Handler&& handler )
    requires detail::invocable_with_expected< Handler, Mutex > && concepts::native_async_mutex< Mutex >
{
    if ( mtx.try_lock() ) {
        // Dispatch on the io_context so the handler always fires asynchronously,
        // giving the caller a chance to store any returned handle first.
        auto h = std::forward< Handler >( handler );
        boost::asio::dispatch( ioc, [ &mtx, h = std::move( h ) ]() mutable {
            detail::invoke_with_lock( h, std::unique_lock( mtx, std::adopt_lock ) );
        } );
        return;
    }

    // Register as a waiter so unlock() will trigger the async event (fast-path types only)
    auto dup_fd = detail::duplicate_native_handle( mtx.native_handle() );

    using async_acquire_op = detail::async_acquire_op< Mutex, Handler >;
    auto op = std::make_shared< async_acquire_op >( ioc, dup_fd, mtx, std::forward< Handler >( handler ) );
    op->start_wait();
}

/// @brief Asynchronously acquires @p mtx and invokes @p handler with an
///        `expected<unique_lock, error_code>` result, returning a cancellable handle.
///
/// Identical to the non-cancellable `async_acquire` overload in all
/// respects, except:
///
/// - Returns a `boost_asio_acquire_handle` that exposes a `cancel()` method.
/// - Destroying the handle automatically cancels any pending wait; the
///   handler is called with `errc::operation_canceled`.
/// - On cancellation @p handler is called with `errc::operation_canceled`.
///
/// @param ioc     Boost.ASIO io_context for async operations.
/// @param mtx     A mutex satisfying `native_async_mutex` with `native_handle()`.
/// @param handler Callable invoked with `expected<std::unique_lock<Mutex>, std::error_code>`.
/// @return Handle allowing cancellation of the pending acquire operation.
///
/// Example
/// -------
/// @code
///   {
///       auto handle = nova::sync::async_acquire_cancellable(ioc, mtx,
///           [](auto result) {
///               if (result) {
///                   // lock acquired
///               } else if (result.error() == std::errc::operation_canceled) {
///                   // cancelled
///               }
///           });
///       // From any thread:
///       handle.cancel();
///   } // Handle destroyed here — operation automatically cancelled if still pending
/// @endcode
template < typename Mutex, typename Handler >
NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS auto
async_acquire_cancellable( boost::asio::io_context& ioc, Mutex& mtx, Handler&& handler )
    -> boost_asio_acquire_handle< Mutex, Handler >
    requires detail::invocable_with_expected< Handler, Mutex > && concepts::native_async_mutex< Mutex >
{
    using Op = detail::async_acquire_op< Mutex, Handler >;

    if ( mtx.try_lock() ) {
        boost::asio::dispatch( ioc, [ &, handler = std::forward< Handler >( handler ) ]() mutable {
            detail::invoke_with_lock( handler, std::unique_lock( mtx, std::adopt_lock ) );
        } );
        return boost_asio_acquire_handle< Mutex, Handler > {};
    }

    auto dup_fd = nova::sync::detail::duplicate_native_handle( mtx.native_handle() );
    auto op     = std::make_shared< Op >( ioc, dup_fd, mtx, std::forward< Handler >( handler ) );
    op->start_wait();
    return boost_asio_acquire_handle { op };
}

} // namespace nova::sync

#endif // __has_include( <boost/asio.hpp>) && defined( NOVA_SYNC_HAS_EXPECTED )
