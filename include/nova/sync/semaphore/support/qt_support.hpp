// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file semaphore/support/qt_support.hpp
///
/// Asynchronous semaphore acquire helpers built on the Qt event loop.
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
///   auto context = QCoreApplication::instance();
///
///   nova::sync::qt_async_acquire(sem, context,
///       [](auto result) {
///           if (result) {
///               // token acquired
///           }
///       });
///
///   sem.release();
/// @endcode

#include <nova/sync/semaphore/detail/async_support.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED ) && __has_include( <QCoreApplication> )

#  include <atomic>
#  include <future>
#  include <memory>
#  include <system_error>

#  include <QtCore/QObject>
#  include <QtCore/QPointer>
#  include <QtCore/QThread>

#  if defined( __linux__ ) || defined( __APPLE__ )
#    include <QtCore/QSocketNotifier>
#  elif defined( _WIN32 )
#    include <QtCore/QWinEventNotifier>
#  endif

#  include <nova/sync/detail/native_handle_support.hpp>
#  include <nova/sync/detail/syscall.hpp>
#  include <nova/sync/semaphore/concepts.hpp>

namespace nova::sync {

namespace detail {

// Platform-specific notifier alias
#  if defined( __linux__ ) || defined( __APPLE__ )
using QtSemaphoreNotifier = QSocketNotifier;
#  elif defined( _WIN32 )
using QtSemaphoreNotifier = QWinEventNotifier;
#  endif

inline void delete_semaphore_notifier( QtSemaphoreNotifier* notifier )
{
    if ( !notifier )
        return;

    notifier->disconnect();

    if ( notifier->thread()->isCurrentThread() )
        delete notifier;
    else
        notifier->deleteLater();
}

// ---------------------------------------------------------------------------
// Non-cancellable async acquire op
// ---------------------------------------------------------------------------

template < typename Semaphore, typename Handler >
struct qt_semaphore_acquire_op : std::enable_shared_from_this< qt_semaphore_acquire_op< Semaphore, Handler > >
{
    Semaphore&                      sem_;
    QObject*                        context_;
    Handler                         handler_;
    QPointer< QtSemaphoreNotifier > notifier_;
#  if defined( __linux__ ) || defined( __APPLE__ )
    detail::scoped_file_descriptor dup_fd_;
#  endif

    qt_semaphore_acquire_op( Semaphore& sem, QObject* context, Handler&& handler ) :
        sem_( sem ),
        context_( context ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~qt_semaphore_acquire_op()
    {
        delete_semaphore_notifier( notifier_ );
    }

    void start()
    {
        if ( sem_.try_acquire() ) {
            deliver_success();
            return;
        }

        create_and_arm_notifier();
    }

private:
    void deliver_success()
    {
        QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ]() {
            invoke_semaphore_success( self->handler_ );
        } );
    }

    void create_and_arm_notifier()
    {
#  if defined( __linux__ ) || defined( __APPLE__ )
        dup_fd_ = detail::scoped_file_descriptor::from_dup( sem_.native_handle() );
        if ( !dup_fd_ )
            return;

        notifier_ = new QSocketNotifier( dup_fd_.get(), QSocketNotifier::Read, context_ );
#  elif defined( _WIN32 )
        notifier_ = new QWinEventNotifier( sem_.native_handle(), context_ );
#  endif

        QObject::connect( notifier_, &QtSemaphoreNotifier::activated, [ self = this->shared_from_this() ] {
            self->on_notified();
        } );
        notifier_->setEnabled( true );
    }

    void on_notified()
    {
        if ( notifier_ )
            notifier_->setEnabled( false );

        if ( sem_.try_acquire() ) {
            if ( notifier_ ) {
                delete_semaphore_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_success();
        } else {
            // Spurious — re-arm
            if ( notifier_ )
                notifier_->setEnabled( true );
        }
    }
};

// ---------------------------------------------------------------------------
// Cancellable async acquire op
// ---------------------------------------------------------------------------

template < typename Semaphore, typename Handler >
struct qt_semaphore_acquire_cancellable_op :
    std::enable_shared_from_this< qt_semaphore_acquire_cancellable_op< Semaphore, Handler > >
{
    Semaphore&                      sem_;
    QObject*                        context_;
    Handler                         handler_;
    QPointer< QtSemaphoreNotifier > notifier_;
    std::atomic< bool >             cancellation_requested_ { false };
    std::atomic< bool >             handler_invoked_ { false };
#  if defined( __linux__ ) || defined( __APPLE__ )
    detail::scoped_file_descriptor dup_fd_;
#  endif

    qt_semaphore_acquire_cancellable_op( Semaphore& sem, QObject* context, Handler&& handler ) :
        sem_( sem ),
        context_( context ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~qt_semaphore_acquire_cancellable_op()
    {
        delete_semaphore_notifier( notifier_ );
    }

    void cancel()
    {
        cancellation_requested_.store( true, std::memory_order_release );

        auto self = this->shared_from_this();
        QMetaObject::invokeMethod( context_, [ self ] {
            if ( self->notifier_ ) {
                delete_semaphore_notifier( self->notifier_ );
                self->notifier_ = nullptr;
            }
            self->deliver_cancellation();
        } );
    }

    void start()
    {
        if ( is_cancelled() ) {
            deliver_cancellation();
            return;
        }

        if ( sem_.try_acquire() ) {
            if ( is_cancelled() ) {
                sem_.release();
                deliver_cancellation();
                return;
            }
            deliver_success();
            return;
        }

        create_and_arm_notifier();
    }

private:
    bool is_cancelled() const noexcept
    {
        return cancellation_requested_.load( std::memory_order_acquire );
    }

    bool try_mark_invoked() noexcept
    {
        bool expected = false;
        return handler_invoked_.compare_exchange_strong( expected,
                                                         true,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire );
    }

    void deliver_cancellation()
    {
        if ( try_mark_invoked() ) {
            QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ] {
                invoke_semaphore_error( self->handler_, std::errc::operation_canceled );
            } );
        }
    }

    void deliver_success()
    {
        if ( try_mark_invoked() ) {
            QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ] {
                invoke_semaphore_success( self->handler_ );
            } );
        }
    }

    void create_and_arm_notifier()
    {
#  if defined( __linux__ ) || defined( __APPLE__ )
        dup_fd_ = detail::scoped_file_descriptor::from_dup( sem_.native_handle() );
        if ( !dup_fd_ )
            return;

        notifier_ = new QSocketNotifier( dup_fd_.get(), QSocketNotifier::Read, context_ );
#  elif defined( _WIN32 )
        notifier_ = new QWinEventNotifier( sem_.native_handle(), context_ );
#  endif

        QObject::connect( notifier_, &QtSemaphoreNotifier::activated, [ self = this->shared_from_this() ] {
            self->on_notified();
        } );
        notifier_->setEnabled( true );
    }

    void on_notified()
    {
        if ( notifier_ )
            notifier_->setEnabled( false );

        if ( is_cancelled() ) {
            if ( notifier_ ) {
                delete_semaphore_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_cancellation();
            return;
        }

        if ( sem_.try_acquire() ) {
            if ( notifier_ ) {
                delete_semaphore_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_success();
        } else {
            // Spurious — re-arm
            if ( notifier_ )
                notifier_->setEnabled( true );
        }
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Public API — cancellable handle
// ---------------------------------------------------------------------------

template < typename Semaphore, typename Handler >
class qt_semaphore_acquire_handle
{
public:
    qt_semaphore_acquire_handle() = default;

    explicit qt_semaphore_acquire_handle(
        std::shared_ptr< detail::qt_semaphore_acquire_cancellable_op< Semaphore, Handler > > op ) :
        op_( std::move( op ) )
    {}

    qt_semaphore_acquire_handle( qt_semaphore_acquire_handle&& other ) noexcept :
        op_( std::move( other.op_ ) )
    {}

    qt_semaphore_acquire_handle& operator=( qt_semaphore_acquire_handle&& other ) noexcept
    {
        if ( this != &other ) {
            cancel();
            op_ = std::move( other.op_ );
        }
        return *this;
    }

    qt_semaphore_acquire_handle( const qt_semaphore_acquire_handle& )            = delete;
    qt_semaphore_acquire_handle& operator=( const qt_semaphore_acquire_handle& ) = delete;

    ~qt_semaphore_acquire_handle()
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

private:
    std::shared_ptr< detail::qt_semaphore_acquire_cancellable_op< Semaphore, Handler > > op_;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously acquires a token from @p sem on the event loop of @p context.
template < typename Semaphore, typename Handler >
void qt_async_acquire( Semaphore& sem, QObject* context, Handler&& handler )
    requires detail::invocable_with_semaphore_expected< Handler > && concepts::native_async_semaphore< Semaphore >
{
    using state_type = detail::qt_semaphore_acquire_op< Semaphore, std::decay_t< Handler > >;
    auto state       = std::make_shared< state_type >( sem, context, std::forward< Handler >( handler ) );
    state->start();
}

/// @brief Asynchronously acquires a token from @p sem with cancellation support.
template < typename Semaphore, typename Handler >
auto qt_async_acquire_cancellable( Semaphore& sem, QObject* context, Handler&& handler )
    -> qt_semaphore_acquire_handle< Semaphore, std::decay_t< Handler > >
    requires detail::invocable_with_semaphore_expected< Handler > && concepts::native_async_semaphore< Semaphore >
{
    using state_type = detail::qt_semaphore_acquire_cancellable_op< Semaphore, std::decay_t< Handler > >;
    auto op          = std::make_shared< state_type >( sem, context, std::forward< Handler >( handler ) );
    op->start();
    return qt_semaphore_acquire_handle< Semaphore, std::decay_t< Handler > > { op };
}

/// @brief Asynchronously acquires a token from @p sem and returns a future.
template < typename Semaphore >
std::future< void > qt_async_acquire( Semaphore& sem, QObject* context )
    requires concepts::native_async_semaphore< Semaphore >
{
    using promise_t = std::promise< void >;

    if ( sem.try_acquire() ) {
        promise_t promise;
        promise.set_value();
        return promise.get_future();
    }

    auto promise = std::make_shared< promise_t >();
    auto future  = promise->get_future();

    auto do_wait = std::make_shared< std::function< void() > >();
    *do_wait     = [ &sem, promise = std::move( promise ), do_wait, context ]() mutable {
        auto handler = [ promise = std::move( promise ), do_wait ]( auto result ) mutable {
            if ( result ) {
                promise->set_value();
                *do_wait = nullptr;
            } else {
                // spurious — retry
                std::invoke( *do_wait );
            }
        };

        nova::sync::qt_async_acquire( sem, context, std::move( handler ) );
    };

    QMetaObject::invokeMethod( context, [ do_wait = std::move( do_wait ) ] {
        std::invoke( *do_wait );
    } );

    return future;
}

} // namespace nova::sync

#endif // defined( NOVA_SYNC_HAS_EXPECTED ) && __has_include( <QCoreApplication> )
