// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/event/support/boost_asio_support.hpp>

#include "event_types.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helper: run an io_context on a background thread
// ---------------------------------------------------------------------------

struct asio_event_runner
{
    boost::asio::io_context                                                    ioc;
    boost::asio::executor_work_guard< boost::asio::io_context::executor_type > work_guard {
        boost::asio::make_work_guard( ioc ) };
    std::thread thread { [ this ] {
        ioc.run();
    } };

    void stop()
    {
        work_guard.reset();
        if ( thread.joinable() )
            thread.join();
    }

    ~asio_event_runner()
    {
        stop();
    }
};

// ---------------------------------------------------------------------------
// Helper: wait with timeout for an atomic flag
// ---------------------------------------------------------------------------

static bool wait_for_flag( const std::atomic< bool >& flag, std::chrono::milliseconds timeout = 2s )
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( !flag.load() && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );
    return flag.load();
}

// ---------------------------------------------------------------------------
// Tests: explicit expected types
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event: explicit expected types", "[async_event][boost_asio]", NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    evt.signal();

#ifdef NOVA_SYNC_HAS_TL_EXPECTED
    SECTION( "tl::expected" )
    {
        auto fired = std::make_shared< std::atomic< bool > >( false );
        nova::sync::async_wait( runner.ioc, evt, [ fired ]( tl::expected< void, std::error_code > result ) {
            REQUIRE( result );
            *fired = true;
        } );
        REQUIRE( wait_for_flag( *fired ) );
    }
#endif

#ifdef NOVA_SYNC_HAS_STD_EXPECTED
    SECTION( "std::expected" )
    {
        evt.signal(); // re-signal for this section (manual_reset stays set, auto_reset consumed above)
        auto fired = std::make_shared< std::atomic< bool > >( false );
        nova::sync::async_wait( runner.ioc, evt, [ fired ]( std::expected< void, std::error_code > result ) {
            REQUIRE( result );
            *fired = true;
        } );
        REQUIRE( wait_for_flag( *fired ) );
    }
#endif
}

// ---------------------------------------------------------------------------
// Tests: immediate wait (event pre-signaled)
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event: fires immediately when pre-signaled",
                    "[async_event][boost_asio]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    evt.signal();

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_wait( runner.ioc, evt, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    REQUIRE( wait_for_flag( *fired ) );
}

// ---------------------------------------------------------------------------
// Tests: wait fires after signal
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event: fires after signal from another thread",
                    "[async_event][boost_asio]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    auto fired              = std::make_shared< std::atomic< bool > >( false );
    auto signal_sent        = std::make_shared< std::atomic< bool > >( false );
    auto fired_after_signal = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_wait( runner.ioc, evt, [ fired, signal_sent, fired_after_signal ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired_after_signal = signal_sent->load();
        *fired              = true;
    } );

    // Ensure the handler has not fired prematurely
    std::this_thread::sleep_for( 30ms );
    REQUIRE( !fired->load() );

    *signal_sent = true;
    evt.signal();

    REQUIRE( wait_for_flag( *fired ) );
    REQUIRE( fired_after_signal->load() );
}

// ---------------------------------------------------------------------------
// Tests: no early wakeup
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event: no early wakeup while not signaled",
                    "[async_event][boost_asio]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_wait( runner.ioc, evt, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !fired->load() );

    evt.signal();
    REQUIRE( wait_for_flag( *fired ) );
}

// ---------------------------------------------------------------------------
// Tests: future-based API
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event: future-based async_wait", "[async_event][boost_asio]", NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    auto [ descriptor, fut ] = nova::sync::async_wait( runner.ioc, evt );

    evt.signal();

    auto status = fut.wait_for( 2s );
    REQUIRE( status == std::future_status::ready );

    runner.stop();
}

// ---------------------------------------------------------------------------
// Tests: cancellation
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event: async_wait_cancellable - cancel stops wait",
                    "[async_event][boost_asio]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    auto handle = nova::sync::async_wait_cancellable( runner.ioc, evt, [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    REQUIRE( wait_for_flag( *handler_invoked ) );
}

TEMPLATE_TEST_CASE( "async_event: async_wait_cancellable - destructor auto-cancels",
                    "[async_event][boost_asio]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt               evt;
    asio_event_runner runner;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    {
        auto handle = nova::sync::async_wait_cancellable( runner.ioc, evt, [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
        // handle destructs here — should auto-cancel
    }

    REQUIRE( wait_for_flag( *handler_invoked ) );
}

// ---------------------------------------------------------------------------
// Tests: manual_reset_event — multiple concurrent waiters all fire
// ---------------------------------------------------------------------------

TEST_CASE( "async_event: manual_reset_event - signal wakes all waiters", "[async_event][boost_asio]" )
{
    nova::sync::native_manual_reset_event evt;
    asio_event_runner                     runner;

    const int tasks       = 4;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i ) {
        nova::sync::async_wait( runner.ioc, evt, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );
    }

    std::this_thread::sleep_for( 30ms );
    REQUIRE( completions->load() == 0 );

    evt.signal(); // all waiters should wake

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( completions->load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();
    REQUIRE( completions->load() == tasks );
}

// ---------------------------------------------------------------------------
// Stress: signal races async_wait
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event stress: signal races async_wait",
                    "[async_event][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    asio_event_runner runner;

    const int rounds      = 50;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        Evt evt;

        nova::sync::async_wait( runner.ioc, evt, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );

        // Signal immediately — races with watcher setup
        evt.signal();

        // Small sleep to let this round settle before moving to the next
        std::this_thread::sleep_for( 2ms );
    }

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions->load() < rounds && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();
    REQUIRE( completions->load() == rounds );
}

TEMPLATE_TEST_CASE( "async_event stress: cancellation under load",
                    "[async_event][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    asio_event_runner runner;
    Evt               evt;

    const int total     = 16;
    const int to_cancel = total / 2;
    auto      successes = std::make_shared< std::atomic< int > >( 0 );
    auto      cancels   = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < ( total - to_cancel ); ++i ) {
        nova::sync::async_wait( runner.ioc, evt, [ successes ]( auto result ) {
            if ( result )
                ++( *successes );
        } );
    }

    for ( int i = 0; i < to_cancel; ++i ) {
        auto handle = nova::sync::async_wait_cancellable( runner.ioc, evt, [ cancels ]( auto result ) {
            if ( !result )
                ++( *cancels );
        } );
        handle.cancel();
    }

    // Signal enough times for the non-cancelled waiters
    for ( int i = 0; i < ( total - to_cancel ); ++i )
        evt.signal();

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( ( successes->load() + cancels->load() ) < total && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( successes->load() + cancels->load() == total );
    REQUIRE( cancels->load() == to_cancel );
}
