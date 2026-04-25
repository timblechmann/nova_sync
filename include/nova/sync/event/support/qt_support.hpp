// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file event/support/qt_support.hpp
///
/// Asynchronous event-wait helpers built on the Qt event loop.
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
///   auto context = QCoreApplication::instance();
///
///   nova::sync::qt_async_wait(evt, context,
///       [](auto result) {
///           if (result) {
///               // event fired
///           }
///       });
///
///   evt.signal();
/// @endcode

#include <nova/sync/event/detail/async_support.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED ) && __has_include( <QCoreApplication> )

#    include <atomic>
#    include <future>
#    include <memory>
#    include <optional>
#    include <system_error>

#    include <QtCore/QObject>
#    include <QtCore/QPointer>
#    include <QtCore/QThread>

#    if defined( __linux__ ) || defined( __APPLE__ )
#        include <QtCore/QSocketNotifier>
#    elif defined( _WIN32 )
#        include <QtCore/QWinEventNotifier>
#    endif

#    include <nova/sync/detail/native_handle_support.hpp>
#    include <nova/sync/detail/syscall.hpp>
#    include <nova/sync/event/concepts.hpp>

namespace nova::sync {

namespace detail {

// Platform-specific notifier alias (reuse the same alias name as mutex/qt_support)
#    if defined( __linux__ ) || defined( __APPLE__ )
using QtEventNotifier = QSocketNotifier;
#    elif defined( _WIN32 )
using QtEventNotifier = QWinEventNotifier;
#    endif

inline void delete_event_notifier( QtEventNotifier* notifier )
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
// Non-cancellable async wait op
// ---------------------------------------------------------------------------

template < typename Event, typename Handler >
struct qt_event_wait_op : std::enable_shared_from_this< qt_event_wait_op< Event, Handler > >
{
    Event&                      evt_;
    QObject*                    context_;
    Handler                     handler_;
    QPointer< QtEventNotifier > notifier_;
#    if defined( __linux__ ) || defined( __APPLE__ )
    detail::scoped_file_descriptor dup_fd_;
#    endif

    qt_event_wait_op( Event& evt, QObject* context, Handler&& handler ) :
        evt_( evt ),
        context_( context ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~qt_event_wait_op()
    {
        delete_event_notifier( notifier_ );
    }

    void start()
    {
        if ( evt_.try_wait() ) {
            deliver_success();
            return;
        }

        create_and_arm_notifier();
    }

private:
    void deliver_success()
    {
        QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ]() {
            invoke_event_success( self->handler_ );
        } );
    }

    void create_and_arm_notifier()
    {
#    if defined( __linux__ ) || defined( __APPLE__ )
        dup_fd_ = detail::scoped_file_descriptor::from_dup( evt_.native_handle() );
        if ( !dup_fd_ )
            return;

        notifier_ = new QSocketNotifier( dup_fd_.get(), QSocketNotifier::Read, context_ );
#    elif defined( _WIN32 )
        notifier_ = new QWinEventNotifier( evt_.native_handle(), context_ );
#    endif

        QObject::connect( notifier_, &QtEventNotifier::activated, [ self = this->shared_from_this() ] {
            self->on_notified();
        } );
        notifier_->setEnabled( true );
    }

    void on_notified()
    {
        if ( notifier_ )
            notifier_->setEnabled( false );

        if ( evt_.try_wait() ) {
            if ( notifier_ ) {
                delete_event_notifier( notifier_ );
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
// Cancellable async wait op
// ---------------------------------------------------------------------------

template < typename Event, typename Handler >
struct qt_event_wait_cancellable_op : std::enable_shared_from_this< qt_event_wait_cancellable_op< Event, Handler > >
{
    Event&                      evt_;
    QObject*                    context_;
    Handler                     handler_;
    QPointer< QtEventNotifier > notifier_;
    std::atomic< bool >         cancellation_requested_ { false };
    std::atomic< bool >         handler_invoked_ { false };
#    if defined( __linux__ ) || defined( __APPLE__ )
    detail::scoped_file_descriptor dup_fd_;
#    endif

    qt_event_wait_cancellable_op( Event& evt, QObject* context, Handler&& handler ) :
        evt_( evt ),
        context_( context ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~qt_event_wait_cancellable_op()
    {
        delete_event_notifier( notifier_ );
    }

    void cancel()
    {
        cancellation_requested_.store( true, std::memory_order_release );

        auto self = this->shared_from_this();
        QMetaObject::invokeMethod( context_, [ self ] {
            if ( self->notifier_ ) {
                delete_event_notifier( self->notifier_ );
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

        if ( evt_.try_wait() ) {
            if ( is_cancelled() ) {
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
                invoke_event_error( self->handler_, std::errc::operation_canceled );
            } );
        }
    }

    void deliver_success()
    {
        if ( try_mark_invoked() ) {
            QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ] {
                invoke_event_success( self->handler_ );
            } );
        }
    }

    void create_and_arm_notifier()
    {
#    if defined( __linux__ ) || defined( __APPLE__ )
        dup_fd_ = detail::scoped_file_descriptor::from_dup( evt_.native_handle() );
        if ( !dup_fd_ )
            return;

        notifier_ = new QSocketNotifier( dup_fd_.get(), QSocketNotifier::Read, context_ );
#    elif defined( _WIN32 )
        notifier_ = new QWinEventNotifier( evt_.native_handle(), context_ );
#    endif

        QObject::connect( notifier_, &QtEventNotifier::activated, [ self = this->shared_from_this() ] {
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
                delete_event_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_cancellation();
            return;
        }

        if ( evt_.try_wait() ) {
            if ( notifier_ ) {
                delete_event_notifier( notifier_ );
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

/// @brief Opaque handle returned by `qt_async_wait_cancellable`.
///
/// Exposes a `cancel()` method. Destroying the handle automatically cancels
/// any pending wait, delivering `errc::operation_canceled` to the handler.
///
/// Thread safety: `cancel()` and the destructor are safe to call from any thread.
template < typename Event, typename Handler >
class qt_event_wait_handle
{
public:
    qt_event_wait_handle() = default;

    explicit qt_event_wait_handle( std::shared_ptr< detail::qt_event_wait_cancellable_op< Event, Handler > > op ) :
        op_( std::move( op ) )
    {}

    qt_event_wait_handle( qt_event_wait_handle&& other ) noexcept :
        op_( std::move( other.op_ ) )
    {}

    qt_event_wait_handle& operator=( qt_event_wait_handle&& other ) noexcept
    {
        if ( this != &other ) {
            cancel();
            op_ = std::move( other.op_ );
        }
        return *this;
    }

    qt_event_wait_handle( const qt_event_wait_handle& )            = delete;
    qt_event_wait_handle& operator=( const qt_event_wait_handle& ) = delete;

    ~qt_event_wait_handle()
    {
        cancel();
    }

    /// @brief Cancel the pending wait. Safe to call from any thread. Idempotent.
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
    std::shared_ptr< detail::qt_event_wait_cancellable_op< Event, Handler > > op_;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously waits for @p evt on the event loop of @p context and
///        invokes @p handler with an `expected<void, error_code>` result.
///
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param context QObject whose thread's event loop processes the operation.
/// @param handler Callable invoked with `expected<void, std::error_code>`.
template < typename Event, typename Handler >
void qt_async_wait( Event& evt, QObject* context, Handler&& handler )
    requires detail::invocable_with_void_expected< Handler > && concepts::native_async_event< Event >
{
    using state_type = detail::qt_event_wait_op< Event, std::decay_t< Handler > >;
    auto state       = std::make_shared< state_type >( evt, context, std::forward< Handler >( handler ) );
    state->start();
}

/// @brief Asynchronously waits for @p evt and returns a cancellable handle.
///
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param context QObject whose thread's event loop processes the operation.
/// @param handler Callable invoked with `expected<void, std::error_code>`.
/// @return Handle allowing cancellation of the pending wait.
template < typename Event, typename Handler >
auto qt_async_wait_cancellable( Event& evt, QObject* context, Handler&& handler )
    -> qt_event_wait_handle< Event, std::decay_t< Handler > >
    requires detail::invocable_with_void_expected< Handler > && concepts::native_async_event< Event >
{
    using state_type = detail::qt_event_wait_cancellable_op< Event, std::decay_t< Handler > >;
    auto op          = std::make_shared< state_type >( evt, context, std::forward< Handler >( handler ) );
    op->start();
    return qt_event_wait_handle< Event, std::decay_t< Handler > > { op };
}

/// @brief Asynchronously waits for @p evt and returns a future.
///
/// @param evt     An event satisfying `native_async_event` with `native_handle()`.
/// @param context QObject whose thread's event loop processes the operation.
/// @return `std::future<void>` that becomes ready when the event fires.
template < typename Event >
std::future< void > qt_async_wait( Event& evt, QObject* context )
    requires concepts::native_async_event< Event >
{
    using promise_t = std::promise< void >;

    if ( evt.try_wait() ) {
        promise_t promise;
        promise.set_value();
        return promise.get_future();
    }

    auto promise = std::make_shared< promise_t >();
    auto future  = promise->get_future();

    auto do_wait = std::make_shared< std::function< void() > >();
    *do_wait     = [ &evt, promise = std::move( promise ), do_wait, context ]() mutable {
        auto handler = [ promise = std::move( promise ), do_wait ]( auto result ) mutable {
            if ( result ) {
                promise->set_value();
                *do_wait = nullptr;
            } else {
                // spurious — retry
                std::invoke( *do_wait );
            }
        };

        nova::sync::qt_async_wait( evt, context, std::move( handler ) );
    };

    QMetaObject::invokeMethod( context, [ do_wait = std::move( do_wait ) ] {
        std::invoke( *do_wait );
    } );

    return future;
}

} // namespace nova::sync

#endif // defined( NOVA_SYNC_HAS_EXPECTED ) && __has_include( <QCoreApplication> )
