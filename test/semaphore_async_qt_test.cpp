// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#ifdef NOVA_SYNC_HAS_QT

#  include <catch2/catch_all.hpp>

#  include "semaphore_types.hpp"

#  include <nova/sync/semaphore/support/qt_support.hpp>

#  include <atomic>
#  include <chrono>
#  include <memory>
#  include <system_error>
#  include <thread>

#  include <QtCore/QCoreApplication>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Catch2 listener: create a QCoreApplication before any test runs, once.
// ---------------------------------------------------------------------------

namespace {

int   s_argc   = 0;
char* s_argv0  = nullptr;
char* s_argv[] = { s_argv0 };

struct QtSemAppListener : Catch::EventListenerBase
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

CATCH_REGISTER_LISTENER( QtSemAppListener )

// ---------------------------------------------------------------------------
// Helper: drive the Qt event loop until predicate is satisfied or deadline.
// ---------------------------------------------------------------------------

static void process_events_until( std::chrono::steady_clock::time_point deadline, auto pred )
{
    while ( !pred() && std::chrono::steady_clock::now() < deadline ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 5ms );
        std::this_thread::sleep_for( 1ms );
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (Qt): fires immediately when pre-released",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 1 );

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::qt_async_acquire( sem, QCoreApplication::instance(), [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return fired->load();
    } );
    REQUIRE( fired->load() );
}

TEMPLATE_TEST_CASE( "async_semaphore (Qt): fires after release from another thread",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 0 );

    auto fired        = std::make_shared< std::atomic< bool > >( false );
    auto release_sent = std::make_shared< std::atomic< bool > >( false );

    nova::sync::qt_async_acquire( sem, QCoreApplication::instance(), [ fired, release_sent ]( auto result ) {
        REQUIRE( result.has_value() );
        REQUIRE( release_sent->load() );
        *fired = true;
    } );

    // Process briefly to confirm no premature fire
    process_events_until( std::chrono::steady_clock::now() + 30ms, [] {
        return false;
    } );
    REQUIRE( !fired->load() );

    *release_sent = true;
    sem.release();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return fired->load();
    } );
    REQUIRE( fired->load() );
}

TEMPLATE_TEST_CASE( "async_semaphore (Qt): no early wakeup while not released",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 0 );

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::qt_async_acquire( sem, QCoreApplication::instance(), [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    process_events_until( std::chrono::steady_clock::now() + 60ms, [] {
        return false;
    } );
    REQUIRE( !fired->load() );

    sem.release();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return fired->load();
    } );
    REQUIRE( fired->load() );
}

TEMPLATE_TEST_CASE( "async_semaphore (Qt): future-based qt_async_acquire",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 0 );

    auto future = nova::sync::qt_async_acquire( sem, QCoreApplication::instance() );

    sem.release();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( future.wait_for( 5ms ) == std::future_status::timeout && std::chrono::steady_clock::now() < deadline ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 1 );
    }

    REQUIRE( future.wait_for( 0ms ) == std::future_status::ready );
}

TEMPLATE_TEST_CASE( "async_semaphore (Qt): qt_async_acquire_cancellable — cancel stops acquire",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 0 );

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    auto handle = nova::sync::qt_async_acquire_cancellable( sem,
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
    REQUIRE( handler_invoked->load() );
}

TEMPLATE_TEST_CASE( "async_semaphore (Qt): qt_async_acquire_cancellable — destructor auto-cancels",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 0 );

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    {
        auto handle = nova::sync::qt_async_acquire_cancellable( sem,
                                                                QCoreApplication::instance(),
                                                                [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
        // destructor auto-cancels
    }

    process_events_until( std::chrono::steady_clock::now() + 2s, [ & ] {
        return handler_invoked->load();
    } );
    REQUIRE( handler_invoked->load() );
}

TEMPLATE_TEST_CASE( "async_semaphore (Qt): multiple acquires each consume one token",
                    "[async_semaphore][qt]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem sem( 0 );

    const int tasks       = 4;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i ) {
        nova::sync::qt_async_acquire( sem, QCoreApplication::instance(), [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );
    }

    // Confirm none fire while unreleased
    process_events_until( std::chrono::steady_clock::now() + 50ms, [] {
        return false;
    } );
    REQUIRE( completions->load() == 0 );

    for ( int i = 0; i < tasks; ++i )
        sem.release();

    process_events_until( std::chrono::steady_clock::now() + 5s, [ & ] {
        return completions->load() >= tasks;
    } );
    REQUIRE( completions->load() == tasks );
}

// ---------------------------------------------------------------------------
// Stress
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (Qt) stress: release races qt_async_acquire",
                    "[async_semaphore][qt][stress]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;

    const int rounds      = 50;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        auto sem = std::make_shared< Sem >( 0 );

        nova::sync::qt_async_acquire( *sem, QCoreApplication::instance(), [ completions, sem ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );

        sem->release();

        process_events_until( std::chrono::steady_clock::now() + 2ms, [] {
            return false;
        } );
    }

    process_events_until( std::chrono::steady_clock::now() + 10s, [ & ] {
        return completions->load() >= rounds;
    } );
    REQUIRE( completions->load() == rounds );
}

#endif // NOVA_SYNC_HAS_QT
