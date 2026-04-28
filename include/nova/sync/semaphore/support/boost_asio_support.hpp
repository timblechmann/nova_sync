// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

/// @file semaphore/support/boost_asio_support.hpp
/// Asynchronous semaphore acquire helpers built on Boost.ASIO.
///
/// Handler signature
/// -----------------
/// @code
///   void handler(expected<void, std::error_code> result);
/// @endcode
///
/// - **Success** (`result.has_value()`): A token was acquired.
/// - **Cancellation / error** (`!result.has_value()`): `result.error()` holds the error.

#pragma once

#include <nova/sync/semaphore/concepts.hpp>
#include <nova/sync/semaphore/detail/async_support.hpp>

#if __has_include( <boost/asio.hpp>) && defined( NOVA_SYNC_HAS_EXPECTED )

#  include <atomic>
#  include <future>
#  include <memory>
#  include <system_error>

#  include <boost/asio/dispatch.hpp>
#  include <boost/asio/io_context.hpp>

#  include <nova/sync/detail/syscall.hpp>

#  if defined( __linux__ ) || defined( __APPLE__ )
#    include <boost/asio/posix/stream_descriptor.hpp>
#  elif defined( _WIN32 )
#    include <boost/asio/windows/object_handle.hpp>
#  endif

namespace nova::sync {

namespace detail {

#  if defined( __linux__ ) || defined( __APPLE__ )
using semaphore_stream_descriptor = boost::asio::posix::stream_descriptor;

template < typename Handler >
auto async_wait_semaphore( boost::asio::posix::stream_descriptor& descriptor, Handler&& handler )
{
    return descriptor.async_wait( boost::asio::posix::stream_descriptor::wait_read, std::forward< Handler >( handler ) );
}
#  elif defined( _WIN32 )
using semaphore_stream_descriptor = boost::asio::windows::object_handle;

template < typename Handler >
auto async_wait_semaphore( boost::asio::windows::object_handle& descriptor, Handler&& handler )
{
    return descriptor.async_wait( std::forward< Handler >( handler ) );
}
#  endif

// Non-cancellable async acquire op
template < typename Semaphore, typename Handler >
struct async_semaphore_acquire_op : std::enable_shared_from_this< async_semaphore_acquire_op< Semaphore, Handler > >
{
    std::shared_ptr< semaphore_stream_descriptor > sd_;
    Semaphore&                                     sem_;
    Handler                                        handler_;

    async_semaphore_acquire_op( boost::asio::io_context& ioc, auto dup_handle, Semaphore& sem, Handler&& handler ) :
        sd_( std::make_shared< semaphore_stream_descriptor >( ioc, dup_handle ) ),
        sem_( sem ),
        handler_( std::forward< Handler >( handler ) )
    {}

    void start_wait()
    {
        async_wait_semaphore( *sd_, [ self = this->shared_from_this() ]( const boost::system::error_code& bec ) {
            if ( bec ) {
                return invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
            }

#  if defined( _WIN32 )
            // On Windows, object_handle::async_wait atomically acquires the semaphore.
            return invoke_semaphore_success( self->handler_ );
#  else
            if ( self->sem_.try_acquire() ) {
                return invoke_semaphore_success( self->handler_ );
            }
            self->start_wait(); // spurious wakeup — retry
#  endif
        } );
    }

    void cancel()
    {
        sd_->cancel();
    }
};

// Cancellable async acquire op
template < typename Semaphore, typename Handler >
struct async_semaphore_acquire_cancellable_op :
    std::enable_shared_from_this< async_semaphore_acquire_cancellable_op< Semaphore, Handler > >
{
    std::shared_ptr< semaphore_stream_descriptor > sd_;
    Semaphore&                                     sem_;
    Handler                                        handler_;
    std::atomic< bool >                            cancellation_requested_ { false };
    std::atomic< bool >                            handler_invoked_ { false };

    async_semaphore_acquire_cancellable_op( boost::asio::io_context& ioc,
                                            auto                     dup_handle,
                                            Semaphore&               sem,
                                            Handler&&                handler ) :
        sd_( std::make_shared< semaphore_stream_descriptor >( ioc, dup_handle ) ),
        sem_( sem ),
        handler_( std::forward< Handler >( handler ) )
    {}

    void cancel()
    {
        cancellation_requested_.store( true, std::memory_order_release );
        sd_->cancel();
    }

    void start_wait()
    {
        async_wait_semaphore( *sd_, [ self = this->shared_from_this() ]( const boost::system::error_code& bec ) {
            if ( self->is_cancelled() ) {
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
                return;
            }

            if ( bec ) {
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
                return;
            }

#  if defined( _WIN32 )
            {
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_semaphore_success( self->handler_ );
            }
#  else
            if ( self->sem_.try_acquire() ) {
                bool expected = false;
                if ( self->handler_invoked_.compare_exchange_strong(
                         expected, true, std::memory_order_release, std::memory_order_acquire ) )
                    invoke_semaphore_success( self->handler_ );
                return;
            }
            self->start_wait(); // spurious wakeup — retry
#  endif
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

template < typename Semaphore, typename Handler >
struct boost_asio_semaphore_acquire_handle
{
    std::shared_ptr< detail::async_semaphore_acquire_cancellable_op< Semaphore, Handler > > op_;

    boost_asio_semaphore_acquire_handle() = default;

    explicit boost_asio_semaphore_acquire_handle(
        std::shared_ptr< detail::async_semaphore_acquire_cancellable_op< Semaphore, Handler > > op ) :
        op_( std::move( op ) )
    {}

    boost_asio_semaphore_acquire_handle( boost_asio_semaphore_acquire_handle&& ) noexcept            = default;
    boost_asio_semaphore_acquire_handle( const boost_asio_semaphore_acquire_handle& )                = delete;
    boost_asio_semaphore_acquire_handle& operator=( const boost_asio_semaphore_acquire_handle& )     = delete;
    boost_asio_semaphore_acquire_handle& operator=( boost_asio_semaphore_acquire_handle&& ) noexcept = default;

    ~boost_asio_semaphore_acquire_handle()
    {
        cancel();
    }

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

template < typename Semaphore >
struct async_semaphore_acquire_future_state
{
    ~async_semaphore_acquire_future_state()
    {
        if ( descriptor )
            descriptor->cancel();
    }

    std::shared_ptr< detail::semaphore_stream_descriptor > descriptor;
    std::future< void >                                    future;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously acquires a token from @p sem and invokes @p handler.
template < typename Semaphore, typename Handler >
void async_acquire( boost::asio::io_context& ioc, Semaphore& sem, Handler&& handler )
    requires detail::invocable_with_semaphore_expected< Handler > && concepts::native_async_semaphore< Semaphore >
{
    if ( sem.try_acquire() ) {
        auto h = std::forward< Handler >( handler );
        boost::asio::dispatch( ioc, [ h = std::move( h ) ]() mutable {
            detail::invoke_semaphore_success( h );
        } );
        return;
    }

    auto dup_handle = detail::duplicate_native_handle( sem.native_handle() );

    using Op = detail::async_semaphore_acquire_op< Semaphore, std::decay_t< Handler > >;
    auto op  = std::make_shared< Op >( ioc, dup_handle, sem, std::forward< Handler >( handler ) );
    op->start_wait();
}

/// @brief Asynchronously acquires a token from @p sem with cancellation support.
template < typename Semaphore, typename Handler >
auto async_acquire_cancellable( boost::asio::io_context& ioc, Semaphore& sem, Handler&& handler )
    -> boost_asio_semaphore_acquire_handle< Semaphore, std::decay_t< Handler > >
    requires detail::invocable_with_semaphore_expected< Handler > && concepts::native_async_semaphore< Semaphore >
{
    using Op = detail::async_semaphore_acquire_cancellable_op< Semaphore, std::decay_t< Handler > >;

    if ( sem.try_acquire() ) {
        boost::asio::dispatch( ioc, [ handler = std::forward< Handler >( handler ) ]() mutable {
            detail::invoke_semaphore_success( handler );
        } );
        return boost_asio_semaphore_acquire_handle< Semaphore, std::decay_t< Handler > > {};
    }

    auto dup_handle = detail::duplicate_native_handle( sem.native_handle() );
    auto op         = std::make_shared< Op >( ioc, dup_handle, sem, std::forward< Handler >( handler ) );
    op->start_wait();
    return boost_asio_semaphore_acquire_handle< Semaphore, std::decay_t< Handler > > { op };
}

/// @brief Asynchronously acquires a token from @p sem and returns a future.
template < typename Semaphore >
async_semaphore_acquire_future_state< Semaphore > async_acquire( boost::asio::io_context& ioc, Semaphore& sem )
    requires concepts::native_async_semaphore< Semaphore >
{
    using promise_t = std::promise< void >;

    if ( sem.try_acquire() ) {
        promise_t promise;
        promise.set_value();
        return async_semaphore_acquire_future_state< Semaphore > { {}, promise.get_future() };
    }

    auto dup_handle = detail::duplicate_native_handle( sem.native_handle() );
    auto sd         = std::make_shared< detail::semaphore_stream_descriptor >( ioc, dup_handle );

#  ifdef __cpp_lib_move_only_function
    auto promise = promise_t();
    auto future  = promise.get_future();

    auto do_wait = std::make_shared< std::move_only_function< void() > >();
    *do_wait     = [ &sem, sd, promise = std::move( promise ), do_wait ]() mutable {
        detail::async_wait_semaphore(
            *sd, [ &sem, sd, promise = std::move( promise ), do_wait ]( const boost::system::error_code& ec ) mutable {
            if ( ec ) {
                *do_wait = nullptr;
                return;
            }

#    if defined( _WIN32 )
            promise.set_value();
            *do_wait = nullptr;
#    else
            if ( sem.try_acquire() ) {
                promise.set_value();
                *do_wait = nullptr;
            } else {
                std::invoke( *do_wait );
            }
#    endif
        } );
    };
#  else
    auto promise = std::make_shared< promise_t >();
    auto future  = promise->get_future();

    auto do_wait = std::make_shared< std::function< void() > >();
    *do_wait     = [ &sem, sd, promise, do_wait ]() mutable {
        detail::async_wait_semaphore( *sd, [ &sem, sd, promise, do_wait ]( const boost::system::error_code& ec ) mutable {
            if ( ec ) {
                *do_wait = nullptr;
                return;
            }

#    if defined( _WIN32 )
            promise->set_value();
            *do_wait = nullptr;
#    else
            if ( sem.try_acquire() ) {
                promise->set_value();
                *do_wait = nullptr;
            } else {
                std::invoke( *do_wait );
            }
#    endif
        } );
    };
#  endif

    std::invoke( *do_wait );

    return async_semaphore_acquire_future_state< Semaphore > { std::move( sd ), std::move( future ) };
}

} // namespace nova::sync

#endif // __has_include( <boost/asio.hpp>) && defined( NOVA_SYNC_HAS_EXPECTED )
