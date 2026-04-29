// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file qt_support.hpp
///
/// Asynchronous mutex acquisition helpers built on the Qt event loop.
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
///   auto context = QCoreApplication::instance();
///
///   nova::sync::qt_async_acquire(mtx, context,
///       [](auto result) {
///           if (result) {
///               // lock acquired
///           }
///       });
/// @endcode

#include <nova/sync/mutex/detail/async_support.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED ) && __has_include( <QCoreApplication> )

#  include <atomic>
#  include <future>
#  include <memory>
#  include <mutex>
#  include <optional>
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
#  include <nova/sync/mutex/concepts.hpp>
#  include <nova/sync/mutex/detail/async_support.hpp>

namespace nova::sync {

// ---------------------------------------------------------------------------
// detail
// ---------------------------------------------------------------------------

namespace detail {

// Platform-specific notifier alias
#  if defined( __linux__ ) || defined( __APPLE__ )
using QtNotifier = QSocketNotifier;
#  elif defined( _WIN32 )
using QtNotifier = QWinEventNotifier;
#  endif

inline void delete_notifier( QtNotifier* notifier )
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

template < typename Mutex, typename Handler >
struct qt_acquire_op : std::enable_shared_from_this< qt_acquire_op< Mutex, Handler > >
{
    Mutex&                                               mtx_;
    QObject*                                             context_;
    Handler                                              handler_;
    QPointer< QtNotifier >                               notifier_;
    std::optional< detail::async_waiter_guard< Mutex > > waiter_guard_;
#  if defined( __linux__ ) || defined( __APPLE__ )
    detail::scoped_file_descriptor dup_fd_;
#  endif

    qt_acquire_op( Mutex& mtx, QObject* context, Handler&& handler ) :
        mtx_( mtx ),
        context_( context ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~qt_acquire_op()
    {
        delete_notifier( notifier_ );
    }

    // Async acquire: lock ownership is transferred to the handler via Qt's
    // event loop; the analyzer cannot track it across that boundary.
    NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS void start()
    {
        if ( mtx_.try_lock() ) {
            deliver_success();
            return;
        }

        // Register as a waiter so unlock() will trigger the notifier
        waiter_guard_.emplace( mtx_ );

        create_and_arm_notifier();
    }

private:
    void deliver_success()
    {
        QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ]() {
            invoke_with_lock( self->handler_, std::unique_lock< Mutex >( self->mtx_, std::adopt_lock ) );
        } );
    }

    void create_and_arm_notifier()
    {
#  if defined( __linux__ ) || defined( __APPLE__ )
        dup_fd_ = detail::scoped_file_descriptor::from_dup( mtx_.native_handle() );
        if ( !dup_fd_ )
            return; // dup failed, can't proceed

        notifier_ = new QSocketNotifier( dup_fd_.get(), QSocketNotifier::Read, context_ );
#  elif defined( _WIN32 )
        notifier_ = new QWinEventNotifier( mtx_.native_handle(), context_ );
#  endif

        QObject::connect( notifier_, &QtNotifier::activated, [ self = this->shared_from_this() ] {
            self->on_notified();
        } );
        notifier_->setEnabled( true );
    }

    void on_notified() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( notifier_ )
            notifier_->setEnabled( false );

        if ( waiter_guard_->try_acquire() ) {
            // Successfully acquired — deliver result
            if ( notifier_ ) {
                delete_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_success();
        } else {
            // Spurious wakeup — re-arm notifier
            if ( notifier_ )
                notifier_->setEnabled( true );
        }
    }
};

// ---------------------------------------------------------------------------
// Cancellable async acquire op
// ---------------------------------------------------------------------------

template < typename Mutex, typename Handler >
struct qt_acquire_cancellable_op : std::enable_shared_from_this< qt_acquire_cancellable_op< Mutex, Handler > >
{
    Mutex&                                               mtx_;
    QObject*                                             context_;
    Handler                                              handler_;
    QPointer< QtNotifier >                               notifier_;
    std::atomic< bool >                                  cancellation_requested_ { false };
    std::atomic< bool >                                  handler_invoked_ { false };
    std::optional< detail::async_waiter_guard< Mutex > > waiter_guard_;
#  if defined( __linux__ ) || defined( __APPLE__ )
    detail::scoped_file_descriptor dup_fd_;
#  endif

    qt_acquire_cancellable_op( Mutex& mtx, QObject* context, Handler&& handler ) :
        mtx_( mtx ),
        context_( context ),
        handler_( std::forward< Handler >( handler ) )
    {}

    ~qt_acquire_cancellable_op()
    {
        delete_notifier( notifier_ );
    }

    void cancel()
    {
        cancellation_requested_.store( true, std::memory_order_release );

        auto self = this->shared_from_this();
        QMetaObject::invokeMethod( context_, [ self ] {
            if ( self->notifier_ ) {
                delete_notifier( self->notifier_ );
                self->notifier_ = nullptr;
            }
            self->deliver_cancellation();
        } );
    }

    // Async acquire: lock ownership is transferred across Qt's event loop;
    // the analyzer cannot track it, so we opt out here.
    NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS void start()
    {
        if ( is_cancelled() ) {
            deliver_cancellation();
            return;
        }

        if ( mtx_.try_lock() ) {
            // Double-check cancel flag after acquiring
            if ( is_cancelled() ) {
                mtx_.unlock();
                deliver_cancellation();
                return;
            }

            deliver_success();
            return;
        }

        // Register as a waiter so unlock() will trigger the notifier
        waiter_guard_.emplace( mtx_ );

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
                invoke_with_error< Mutex >( self->handler_, std::make_error_code( std::errc::operation_canceled ) );
            } );
        }
    }

    void deliver_success()
    {
        if ( try_mark_invoked() ) {
            QMetaObject::invokeMethod( context_, [ self = this->shared_from_this() ] {
                invoke_with_lock( self->handler_, std::unique_lock< Mutex >( self->mtx_, std::adopt_lock ) );
            } );
        }
    }

    void create_and_arm_notifier()
    {
#  if defined( __linux__ ) || defined( __APPLE__ )
        dup_fd_ = detail::scoped_file_descriptor::from_dup( mtx_.native_handle() );
        if ( !dup_fd_ )
            return; // dup failed, can't proceed

        notifier_ = new QSocketNotifier( dup_fd_.get(), QSocketNotifier::Read, context_ );
        QObject::connect( notifier_, &QSocketNotifier::activated, [ self = this->shared_from_this() ] {
            self->on_notified();
        } );
#  elif defined( _WIN32 )
        notifier_ = new QWinEventNotifier( mtx_.native_handle(), context_ );
        QObject::connect( notifier_, &QWinEventNotifier::activated, [ self = this->shared_from_this() ] {
            self->on_notified();
        } );
#  endif

        notifier_->setEnabled( true );
    }

    void on_notified() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
    {
        if ( notifier_ )
            notifier_->setEnabled( false );

        if ( is_cancelled() ) {
            waiter_guard_.reset();
            if ( notifier_ ) {
                delete_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_cancellation();
            return;
        }

        if ( waiter_guard_->try_acquire() ) {
            // Lock acquired; guard is already released by try_acquire().
            // Double-check cancel race after acquiring.
            if ( is_cancelled() ) {
                mtx_.unlock();
                if ( notifier_ ) {
                    delete_notifier( notifier_ );
                    notifier_ = nullptr;
                }
                deliver_cancellation();
                return;
            }
            if ( notifier_ ) {
                delete_notifier( notifier_ );
                notifier_ = nullptr;
            }
            deliver_success();
        } else {
            if ( notifier_ )
                notifier_->setEnabled( true );
        }
    }
};

} // namespace detail

// ---------------------------------------------------------------------------
// Public API — cancellable handle
// ---------------------------------------------------------------------------

/// @brief Opaque handle returned by `qt_async_acquire_cancellable`.
///
/// Exposes a `cancel()` method. Destroying the handle automatically cancels
/// any pending wait, delivering `errc::operation_canceled` to the handler.
///
/// Thread safety: `cancel()` and the destructor are safe to call from any thread.
template < typename Mutex, typename Handler >
class qt_acquire_handle
{
public:
    qt_acquire_handle() = default;

    explicit qt_acquire_handle( std::shared_ptr< detail::qt_acquire_cancellable_op< Mutex, Handler > > op ) :
        op_( std::move( op ) )
    {}

    qt_acquire_handle( qt_acquire_handle&& other ) noexcept :
        op_( std::move( other.op_ ) )
    {}
    qt_acquire_handle& operator=( qt_acquire_handle&& other ) noexcept
    {
        if ( this != &other ) {
            cancel();
            op_ = std::move( other.op_ );
        }
        return *this;
    }

    qt_acquire_handle( const qt_acquire_handle& )            = delete;
    qt_acquire_handle& operator=( const qt_acquire_handle& ) = delete;

    ~qt_acquire_handle()
    {
        cancel();
    }

    /// @brief Cancel the pending acquire. Safe to call from any thread. Idempotent.
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
    std::shared_ptr< detail::qt_acquire_cancellable_op< Mutex, Handler > > op_;
};

// ---------------------------------------------------------------------------
// Public API — free functions
// ---------------------------------------------------------------------------

/// @brief Asynchronously acquires @p mtx on the event loop of @p context and
///        invokes @p handler with an `expected<unique_lock, error_code>` result.
///
/// The handler is called exactly once from the context's event loop thread with
/// the signature:
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
/// - @p context must remain live until @p handler fires and must be an object
///   whose event loop is running (typically `QCoreApplication::instance()`).
///
/// @param mtx     A mutex satisfying `native_async_mutex` with `native_handle()`.
/// @param context QObject whose thread's event loop will process the operation.
/// @param handler Callable invoked with `expected<std::unique_lock<Mutex>, std::error_code>`.
template < typename Mutex, typename Handler >
void qt_async_acquire( Mutex& mtx, QObject* context, Handler&& handler )
    requires detail::invocable_with_expected< Handler, Mutex > && concepts::native_async_mutex< Mutex >
{
    using state_type = detail::qt_acquire_op< Mutex, std::decay_t< Handler > >;
    auto state       = std::make_shared< state_type >( mtx, context, std::forward< Handler >( handler ) );
    state->start();
}

/// @brief Asynchronously acquires @p mtx and returns a cancellable handle.
///
/// Identical to the non-cancellable `qt_async_acquire` overload except:
///
/// - Returns a `qt_acquire_handle` exposing a `cancel()` method.
/// - Calling `handle.cancel()` aborts the wait; @p handler is called with
///   `errc::operation_canceled`.
/// - Destroying the handle automatically cancels any pending wait.
///
/// Example
/// -------
/// @code
///   auto handle = nova::sync::qt_async_acquire_cancellable(mtx, context,
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
auto qt_async_acquire_cancellable( Mutex& mtx, QObject* context, Handler&& handler )
    -> qt_acquire_handle< Mutex, std::decay_t< Handler > >
    requires detail::invocable_with_expected< Handler, Mutex > && concepts::native_async_mutex< Mutex >
{
    using state_type = detail::qt_acquire_cancellable_op< Mutex, std::decay_t< Handler > >;
    auto op          = std::make_shared< state_type >( mtx, context, std::forward< Handler >( handler ) );
    op->start();
    return qt_acquire_handle< Mutex, std::decay_t< Handler > > { op };
}

/// @brief Asynchronously acquires @p mtx and returns a future.
///
/// If the mutex is immediately available (`try_lock()` succeeds), the returned
/// future is already ready with a locked `unique_lock`. Otherwise the acquisition
/// is performed asynchronously; once the mutex is acquired the future becomes ready.
///
/// @param mtx     A mutex satisfying `native_async_mutex` with `native_handle()`.
/// @param context QObject whose thread's event loop will process the operation.
/// @return `std::future<std::unique_lock<Mutex>>` that becomes ready when acquired.
template < typename Mutex >
std::future< std::unique_lock< Mutex > > qt_async_acquire( Mutex& mtx, QObject* context )
    requires concepts::native_async_mutex< Mutex >
NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
{
    using promise_t = std::promise< std::unique_lock< Mutex > >;

    if ( mtx.try_lock() ) {
        promise_t promise;
        promise.set_value( std::unique_lock( mtx, std::adopt_lock ) );
        return promise.get_future();
    }

    auto promise = std::make_shared< promise_t >();
    auto future  = promise->get_future();

    auto do_wait = std::make_shared< std::function< void() > >();
    *do_wait     = [ &, promise = std::move( promise ), do_wait, context ]() mutable {
        auto handler = [ promise = std::move( promise ), do_wait ]( auto result ) mutable {
            if ( result ) {
                promise->set_value( std::move( *result ) );
                *do_wait = nullptr; // break cycle
            } else {
                // spurious wakeup, retry
                std::invoke( *do_wait );
            }
        };

        nova::sync::qt_async_acquire( mtx, context, std::move( handler ) );
    };

    QMetaObject::invokeMethod( context, [ do_wait = std::move( do_wait ) ] {
        std::invoke( *do_wait );
    } );

    return future;
}

} // namespace nova::sync

#endif // defined( NOVA_SYNC_HAS_EXPECTED ) && __has_include( <QCoreApplication> )
