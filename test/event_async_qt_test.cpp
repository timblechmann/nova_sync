// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#ifdef NOVA_SYNC_HAS_QT

#    include <catch2/catch_all.hpp>

#    include "event_types.hpp"

#    include <nova/sync/event/support/qt_support.hpp>

#    include <atomic>
#    include <chrono>
#    include <memory>
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

int   s_event_argc   = 0;
char* s_event_argv0  = nullptr;
char* s_event_argv[] = { s_event_argv0 };

struct QtEventAppListener : Catch::EventListenerBase
{
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting( Catch::TestRunInfo const& ) override
    {
        if ( !QCoreApplication::instance() )
            app_ = std::make_unique< QCoreApplication >( s_event_argc, s_event_argv );
    }

    std::unique_ptr< QCoreApplication > app_;
};

} // namespace

CATCH_REGISTER_LISTENER( QtEventAppListener )

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
// Tests: immediate wait (event pre-signaled)
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (qt): fires immediately when pre-signaled",
                    "[async_event][qt]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt evt;

    evt.signal();

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::qt_async_wait( evt, QCoreApplication::instance(), [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    process_events_until( std::chrono::steady_clock::now() + 2s, [ &fired ] {
        return fired->load();
    } );
    REQUIRE( fired->load() );
}

// ---------------------------------------------------------------------------
// Tests: fires after signal from another thread
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (qt): fires after signal from another thread",
                    "[async_event][qt]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt evt;

    auto fired              = std::make_shared< std::atomic< bool > >( false );
    auto signal_sent        = std::make_shared< std::atomic< bool > >( false );
    auto fired_after_signal = std::make_shared< std::atomic< bool > >( false );

    nova::sync::qt_async_wait( evt,
                               QCoreApplication::instance(),
                               [ fired, signal_sent, fired_after_signal ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired_after_signal = signal_sent->load();
        *fired              = true;
    } );

    // Process events briefly — handler should NOT fire yet
    process_events_until( std::chrono::steady_clock::now() + 50ms, [] {
        return false;
    } );
    REQUIRE( !fired->load() );

    // Signal from a background thread
    std::thread( [ &evt, signal_sent ] {
        std::this_thread::sleep_for( 10ms );
        *signal_sent = true;
        evt.signal();
    } ).detach();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ &fired ] {
        return fired->load();
    } );
    REQUIRE( fired->load() );
    REQUIRE( fired_after_signal->load() );
}

// ---------------------------------------------------------------------------
// Tests: no early wakeup
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (qt): no early wakeup while not signaled",
                    "[async_event][qt]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt evt;

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::qt_async_wait( evt, QCoreApplication::instance(), [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    process_events_until( std::chrono::steady_clock::now() + 60ms, [] {
        return false;
    } );
    REQUIRE( !fired->load() );

    evt.signal();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ &fired ] {
        return fired->load();
    } );
    REQUIRE( fired->load() );
}

// ---------------------------------------------------------------------------
// Tests: future-based API
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (qt): future-based qt_async_wait", "[async_event][qt]", NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt evt;

    auto fut = nova::sync::qt_async_wait( evt, QCoreApplication::instance() );

    // Signal from a thread to avoid blocking processEvents indefinitely
    std::thread( [ &evt ] {
        std::this_thread::sleep_for( 10ms );
        evt.signal();
    } ).detach();

    // Drive event loop until future is ready
    process_events_until( std::chrono::steady_clock::now() + 2s, [ &fut ] {
        return fut.wait_for( 0s ) == std::future_status::ready;
    } );

    REQUIRE( fut.wait_for( 0s ) == std::future_status::ready );
}

// ---------------------------------------------------------------------------
// Tests: cancellation
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (qt): qt_async_wait_cancellable — cancel stops wait",
                    "[async_event][qt]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt evt;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    auto handle
        = nova::sync::qt_async_wait_cancellable( evt, QCoreApplication::instance(), [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    process_events_until( std::chrono::steady_clock::now() + 2s, [ &handler_invoked ] {
        return handler_invoked->load();
    } );
    REQUIRE( handler_invoked->load() );
}

TEMPLATE_TEST_CASE( "async_event (qt): qt_async_wait_cancellable — destructor auto-cancels",
                    "[async_event][qt]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt evt;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    {
        auto handle = nova::sync::qt_async_wait_cancellable( evt,
                                                             QCoreApplication::instance(),
                                                             [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
        // handle destructs here — should auto-cancel
    }

    process_events_until( std::chrono::steady_clock::now() + 2s, [ &handler_invoked ] {
        return handler_invoked->load();
    } );
    REQUIRE( handler_invoked->load() );
}

// ---------------------------------------------------------------------------
// Tests: manual_reset — all waiters wake on signal
// ---------------------------------------------------------------------------

TEST_CASE( "async_event (qt): manual_reset_event — signal wakes all waiters", "[async_event][qt]" )
{
    nova::sync::native_manual_reset_event evt;

    const int tasks       = 4;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i ) {
        nova::sync::qt_async_wait( evt, QCoreApplication::instance(), [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );
    }

    // Process briefly — no signal yet
    process_events_until( std::chrono::steady_clock::now() + 50ms, [] {
        return false;
    } );
    REQUIRE( completions->load() == 0 );

    evt.signal(); // all waiters wake

    process_events_until( std::chrono::steady_clock::now() + 2s, [ &completions, tasks ] {
        return completions->load() >= tasks;
    } );
    REQUIRE( completions->load() == tasks );
}

// ---------------------------------------------------------------------------
// Stress: signal races qt_async_wait
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (qt) stress: signal races async_wait",
                    "[async_event][qt][stress]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;

    const int rounds      = 20;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        Evt evt;

        nova::sync::qt_async_wait( evt, QCoreApplication::instance(), [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );

        evt.signal();

        // Drive event loop for a bit between rounds
        process_events_until( std::chrono::steady_clock::now() + 5ms, [] {
            return false;
        } );
    }

    process_events_until( std::chrono::steady_clock::now() + 5s, [ &completions, rounds ] {
        return completions->load() >= rounds;
    } );
    REQUIRE( completions->load() == rounds );
}

#endif // NOVA_SYNC_HAS_QT
