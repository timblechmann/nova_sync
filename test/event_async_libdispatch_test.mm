// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/event/support/libdispatch_support.hpp>

#include "event_types.hpp"

#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dispatch_queue_t make_serial_queue( const char* label )
{
    return dispatch_queue_create( label, DISPATCH_QUEUE_SERIAL );
}

static bool wait_for_flag( const std::atomic< bool >& flag, std::chrono::milliseconds timeout = 2s )
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( !flag.load() && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );
    return flag.load();
}

// ---------------------------------------------------------------------------
// Tests: immediate wait (event pre-signaled)
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (dispatch): fires immediately when pre-signaled",
                    "[async_event][libdispatch]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt              evt;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.immediate" );

    evt.signal();

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_wait( evt, queue, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    REQUIRE( wait_for_flag( *fired ) );
    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: fires after signal
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (dispatch): fires after signal from another thread",
                    "[async_event][libdispatch]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt              evt;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.after_signal" );

    auto fired              = std::make_shared< std::atomic< bool > >( false );
    auto signal_sent        = std::make_shared< std::atomic< bool > >( false );
    auto fired_after_signal = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_wait( evt, queue, [ fired, signal_sent, fired_after_signal ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired_after_signal = signal_sent->load();
        *fired              = true;
    } );

    std::this_thread::sleep_for( 30ms );
    REQUIRE( !fired->load() );

    *signal_sent = true;
    evt.signal();

    REQUIRE( wait_for_flag( *fired ) );
    REQUIRE( fired_after_signal->load() );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: no early wakeup
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (dispatch): no early wakeup while not signaled",
                    "[async_event][libdispatch]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt              evt;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.no_early" );

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_wait( evt, queue, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !fired->load() );

    evt.signal();
    REQUIRE( wait_for_flag( *fired ) );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: future-based API
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (dispatch): future-based async_wait",
                    "[async_event][libdispatch]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt              evt;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.future" );

    auto state = nova::sync::async_wait( evt, queue );

    evt.signal();

    auto status = state.future.wait_for( 2s );
    REQUIRE( status == std::future_status::ready );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: cancellation
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (dispatch): async_wait_cancellable - cancel stops wait",
                    "[async_event][libdispatch]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt              evt;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.cancel" );

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    auto handle = nova::sync::async_wait_cancellable( evt, queue, [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    REQUIRE( wait_for_flag( *handler_invoked ) );

    dispatch_release( queue );
}

TEMPLATE_TEST_CASE( "async_event (dispatch): async_wait_cancellable - destructor auto-cancels",
                    "[async_event][libdispatch]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt = TestType;
    Evt              evt;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.dtor_cancel" );

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    {
        auto handle = nova::sync::async_wait_cancellable( evt, queue, [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
        // handle destructs here
    }

    REQUIRE( wait_for_flag( *handler_invoked ) );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: manual_reset — all waiters wake on signal
// ---------------------------------------------------------------------------

TEST_CASE( "async_event (dispatch): manual_reset_event - signal wakes all waiters", "[async_event][libdispatch]" )
{
    nova::sync::native_manual_reset_event evt;
    dispatch_queue_t                      queue = make_serial_queue( "nova.test.event.manual_multi" );

    const int tasks       = 4;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i ) {
        nova::sync::async_wait( evt, queue, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );
    }

    std::this_thread::sleep_for( 30ms );
    REQUIRE( completions->load() == 0 );

    evt.signal();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( completions->load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    REQUIRE( completions->load() == tasks );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Stress: signal races async_wait
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_event (dispatch) stress: signal races async_wait",
                    "[async_event][libdispatch][stress]",
                    NOVA_SYNC_ASYNC_EVENT_TYPES )
{
    using Evt              = TestType;
    dispatch_queue_t queue = make_serial_queue( "nova.test.event.stress_race" );

    const int rounds      = 50;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        Evt evt;

        nova::sync::async_wait( evt, queue, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );

        evt.signal();

        std::this_thread::sleep_for( 2ms );
    }

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions->load() < rounds && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    REQUIRE( completions->load() == rounds );

    dispatch_release( queue );
}
