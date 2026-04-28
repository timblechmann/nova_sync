// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/semaphore/support/libdispatch_support.hpp>

#include "semaphore_types.hpp"

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

static bool wait_for_flag( const std::atomic< bool >& flag, std::chrono::milliseconds timeout = 10s )
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while ( !flag.load() && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );
    return flag.load();
}

// ---------------------------------------------------------------------------
// Tests: immediate acquire (pre-released)
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): fires immediately when pre-released",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 1 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.immediate" );

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_acquire( sem, queue, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    REQUIRE( wait_for_flag( *fired ) );
    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: acquire fires after release
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): fires after release from another thread",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 0 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.after_release" );

    auto fired        = std::make_shared< std::atomic< bool > >( false );
    auto release_sent = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_acquire( sem, queue, [ fired, release_sent ]( auto result ) {
        REQUIRE( result.has_value() );
        REQUIRE( release_sent->load() );
        *fired = true;
    } );

    std::this_thread::sleep_for( 30ms );
    REQUIRE( !fired->load() );

    *release_sent = true;
    sem.release();

    REQUIRE( wait_for_flag( *fired ) );
    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: no early wakeup
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): no early wakeup while not released",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 0 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.no_early" );

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_acquire( sem, queue, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !fired->load() );

    sem.release();
    REQUIRE( wait_for_flag( *fired ) );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: future-based API
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): future-based async_acquire",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 0 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.future" );

    auto state = nova::sync::async_acquire( sem, queue );

    sem.release();

    auto status = state.future.wait_for( 10s );
    REQUIRE( status == std::future_status::ready );

    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Tests: cancellation
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): async_acquire_cancellable — cancel stops acquire",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 0 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.cancel" );

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    auto handle = nova::sync::async_acquire_cancellable( sem, queue, [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    REQUIRE( wait_for_flag( *handler_invoked ) );

    dispatch_release( queue );
}

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): async_acquire_cancellable — destructor auto-cancels",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 0 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.dtor_cancel" );

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    {
        auto handle = nova::sync::async_acquire_cancellable( sem, queue, [ handler_invoked ]( auto result ) {
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
// Tests: multiple concurrent acquires
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch): multiple acquires each consume one token",
                    "[async_semaphore][libdispatch]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem              sem( 0 );
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.multi" );

    const int tasks       = 4;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i ) {
        nova::sync::async_acquire( sem, queue, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );
    }

    std::this_thread::sleep_for( 50ms );
    REQUIRE( completions->load() == 0 );

    for ( int i = 0; i < tasks; ++i )
        sem.release();

    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ( completions->load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    REQUIRE( completions->load() == tasks );
    dispatch_release( queue );
}

// ---------------------------------------------------------------------------
// Stress: release races async_acquire
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore (dispatch) stress: release races async_acquire",
                    "[async_semaphore][libdispatch][stress]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem              = TestType;
    dispatch_queue_t queue = make_serial_queue( "nova.test.sem.stress_race" );

    const int rounds      = 100;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        Sem sem( 0 );

        nova::sync::async_acquire( sem, queue, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );

        sem.release();

        std::this_thread::sleep_for( 1ms );
    }

    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ( completions->load() < rounds && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    REQUIRE( completions->load() == rounds );
    dispatch_release( queue );
}
