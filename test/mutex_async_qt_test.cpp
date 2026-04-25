// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#ifdef NOVA_SYNC_HAS_QT

#    include <catch2/catch_all.hpp>

#    include "mutex_types.hpp"

#    include <nova/sync/mutex/support/qt_support.hpp>

#    include <atomic>
#    include <chrono>
#    include <memory>
#    include <mutex>
#    include <system_error>
#    include <thread>

#    include <QtCore/QCoreApplication>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Catch2 listener: create a QCoreApplication before any test runs, once.
// QCoreApplication must live on (and be constructed from) the main thread —
// which is exactly the thread Catch2 uses to call listeners.
// ---------------------------------------------------------------------------

namespace {

int   s_argc   = 0;
char* s_argv0  = nullptr;
char* s_argv[] = { s_argv0 };

struct QtAppListener : Catch::EventListenerBase
{
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting( Catch::TestRunInfo const& ) override
    {
        if ( !QCoreApplication::instance() )
            app_ = std::make_unique< QCoreApplication >( s_argc, s_argv );
    }

    std::unique_ptr< QCoreApplication > app_;
};

} // namespace

CATCH_REGISTER_LISTENER( QtAppListener )

// ---------------------------------------------------------------------------
// Helper: drive the Qt event loop on the current (main) thread until a
// predicate is satisfied or a deadline is reached.
// ---------------------------------------------------------------------------

static void process_events_until( std::chrono::steady_clock::time_point deadline, auto pred )
{
    while ( !pred() && std::chrono::steady_clock::now() < deadline ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 5ms );
        std::this_thread::sleep_for( 1ms );
    }
}

// ---------------------------------------------------------------------------
// Qt tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "native_async_mutex (Qt): explicit expected types",
                    "[native_async_mutex][qt]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    auto handler_fired = std::make_shared< std::atomic< bool > >( false );

#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
    SECTION( "tl::expected" )
    {
        nova::sync::qt_async_acquire( mtx,
                                      QCoreApplication::instance(),
                                      [ handler_fired ]( tl::expected< std::unique_lock< Mtx >, std::error_code > result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
            *handler_fired = true;
        } );

        process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
            return handler_fired->load();
        } );
        REQUIRE( handler_fired->load() );
    }
#    endif

#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
    SECTION( "std::expected" )
    {
        nova::sync::qt_async_acquire(
            mtx,
            QCoreApplication::instance(),
            [ handler_fired ]( std::expected< std::unique_lock< Mtx >, std::error_code > result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
            *handler_fired = true;
        } );

        process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
            return handler_fired->load();
        } );
        REQUIRE( handler_fired->load() );
    }
#    endif
}

TEMPLATE_TEST_CASE( "native_async_mutex (Qt): async acquire fires after unlock",
                    "[native_async_mutex][qt]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    auto acquired_after_release = std::make_shared< std::atomic< bool > >( false );
    auto mutex_released         = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    nova::sync::qt_async_acquire( mtx, QCoreApplication::instance(), [ acquired_after_release ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
    } );

    // Second waiter records whether mutex was released before it ran.
    nova::sync::qt_async_acquire( mtx,
                                  QCoreApplication::instance(),
                                  [ acquired_after_release, mutex_released ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
        *acquired_after_release = mutex_released->load();
    } );

    REQUIRE( !acquired_after_release->load() );

    // Process events briefly, then release
    process_events_until( std::chrono::steady_clock::now() + 30ms, [] {
        return false;
    } );
    *mutex_released = true;
    mtx.unlock();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return acquired_after_release->load();
    } );

    REQUIRE( acquired_after_release->load() );
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "native_async_mutex (Qt): no early wakeup while locked",
                    "[native_async_mutex][qt]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    auto handler_fired = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    nova::sync::qt_async_acquire( mtx, QCoreApplication::instance(), [ handler_fired ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
        *handler_fired = true;
    } );

    // Process events for 60ms — handler must NOT fire while locked.
    process_events_until( std::chrono::steady_clock::now() + 60ms, [] {
        return false;
    } );
    REQUIRE( !handler_fired->load() );

    mtx.unlock();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return handler_fired->load();
    } );

    REQUIRE( handler_fired->load() );
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "native_async_mutex (Qt): mutual exclusion with async acquires",
                    "[native_async_mutex][qt]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    const int tasks          = 8;
    auto      inside         = std::make_shared< std::atomic< int > >( 0 );
    auto      max_concurrent = std::make_shared< std::atomic< int > >( 0 );
    auto      completions    = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i )
        QMetaObject::invokeMethod( QCoreApplication::instance(), [ &mtx, inside, max_concurrent, completions ] {
            nova::sync::qt_async_acquire( mtx,
                                          QCoreApplication::instance(),
                                          [ inside, max_concurrent, completions ]( auto result ) {
                REQUIRE( result );
                REQUIRE( result->owns_lock() );
                int current  = ++( *inside );
                int expected = max_concurrent->load();
                while ( current > expected && !max_concurrent->compare_exchange_weak( expected, current ) )
                    ;
                --( *inside );
                ++( *completions );
            } );
        }, Qt::QueuedConnection );

    process_events_until( std::chrono::steady_clock::now() + 5s, [ & ] {
        return completions->load() >= tasks;
    } );

    REQUIRE( completions->load() == tasks );
    REQUIRE( *inside == 0 );
    REQUIRE( *max_concurrent == 1 );
}

TEMPLATE_TEST_CASE( "qt_async_acquire_cancellable — cancel delivers error to handler",
                    "[native_async_mutex][qt]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    auto handle = nova::sync::qt_async_acquire_cancellable( mtx,
                                                            QCoreApplication::instance(),
                                                            [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return handler_invoked->load();
    } );

    mtx.unlock();

    REQUIRE( handler_invoked->load() );
}

TEMPLATE_TEST_CASE( "qt_async_acquire_cancellable — destructor auto-cancels",
                    "[native_async_mutex][qt]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    {
        auto handle = nova::sync::qt_async_acquire_cancellable( mtx,
                                                                QCoreApplication::instance(),
                                                                [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
        // Handle goes out of scope — destructor auto-cancels
    }

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return handler_invoked->load();
    } );

    mtx.unlock();

    REQUIRE( handler_invoked->load() );
}

TEMPLATE_TEST_CASE( "qt_async_acquire — futures", "[native_async_mutex][qt]", NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    SECTION( "std::future: ready immediately if mutex available" )
    {
        auto future = nova::sync::qt_async_acquire( mtx, QCoreApplication::instance() );

        REQUIRE( future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready );
        auto lock = future.get();
        REQUIRE( lock.owns_lock() );
    }

    SECTION( "std::future: becomes ready after unlock" )
    {
        mtx.lock();

        auto future = nova::sync::qt_async_acquire( mtx, QCoreApplication::instance() );

        // Future should not be ready while locked
        REQUIRE( future.wait_for( std::chrono::milliseconds( 30 ) ) == std::future_status::timeout );

        mtx.unlock();

        // Future should become ready after unlock
        auto deadline = std::chrono::steady_clock::now() + 2s;
        while ( future.wait_for( std::chrono::milliseconds( 5 ) ) == std::future_status::timeout
                && std::chrono::steady_clock::now() < deadline ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 1 );
        }

        REQUIRE( future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready );
        auto lock = future.get();
        REQUIRE( lock.owns_lock() );
    }
}

// ---------------------------------------------------------------------------
// Stress / edge-case tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "native_async_mutex (Qt) stress: high contention many async waiters",
                    "[native_async_mutex][qt][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    const int tasks          = 32;
    auto      inside         = std::make_shared< std::atomic< int > >( 0 );
    auto      max_concurrent = std::make_shared< std::atomic< int > >( 0 );
    auto      completions    = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i )
        QMetaObject::invokeMethod( QCoreApplication::instance(), [ &mtx, inside, max_concurrent, completions ] {
            nova::sync::qt_async_acquire( mtx,
                                          QCoreApplication::instance(),
                                          [ inside, max_concurrent, completions ]( auto result ) {
                REQUIRE( result );
                REQUIRE( result->owns_lock() );
                int current  = ++( *inside );
                int expected = max_concurrent->load();
                while ( current > expected && !max_concurrent->compare_exchange_weak( expected, current ) )
                    ;
                --( *inside );
                ++( *completions );
            } );
        }, Qt::QueuedConnection );

    process_events_until( std::chrono::steady_clock::now() + 10s, [ & ] {
        return completions->load() >= tasks;
    } );

    REQUIRE( completions->load() == tasks );
    REQUIRE( *inside == 0 );
    REQUIRE( *max_concurrent == 1 );

    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "native_async_mutex (Qt) stress: unlock races CAS acquisition",
                    "[native_async_mutex][qt][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    const int rounds      = 50;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        mtx.lock();

        nova::sync::qt_async_acquire( mtx, QCoreApplication::instance(), [ completions ]( auto result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
            ++( *completions );
        } );

        // Unlock immediately — races with async wakeup
        mtx.unlock();
    }

    process_events_until( std::chrono::steady_clock::now() + 5s, [ & ] {
        return completions->load() >= rounds;
    } );

    REQUIRE( completions->load() == rounds );
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "native_async_mutex (Qt) stress: no stray notifications after acquire cycles",
                    "[native_async_mutex][qt][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx mtx;

    const int rounds      = 20;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        auto future = nova::sync::qt_async_acquire( mtx, QCoreApplication::instance() );

        auto deadline = std::chrono::steady_clock::now() + 2s;
        while ( future.wait_for( 5ms ) == std::future_status::timeout && std::chrono::steady_clock::now() < deadline )
            QCoreApplication::processEvents( QEventLoop::AllEvents, 1 );

        if ( future.wait_for( 0ms ) == std::future_status::ready ) {
            auto lock = future.get();
            REQUIRE( lock.owns_lock() );
            ++( *completions );
        }
    }

    REQUIRE( completions->load() == rounds );

    // No stray notifications — mutex must be immediately acquirable
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

#endif // NOVA_SYNC_HAS_QT
