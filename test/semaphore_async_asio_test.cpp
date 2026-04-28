// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/semaphore/support/boost_asio_support.hpp>

#include "semaphore_types.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helper: run an io_context on a background thread
// ---------------------------------------------------------------------------

struct asio_semaphore_runner
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

    ~asio_semaphore_runner()
    {
        stop();
    }
};

// ---------------------------------------------------------------------------
// Helper: wait with timeout for an atomic flag
// ---------------------------------------------------------------------------

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

TEMPLATE_TEST_CASE( "async_semaphore: fires immediately when pre-released",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 1 );
    asio_semaphore_runner runner;

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_acquire( runner.ioc, sem, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    REQUIRE( wait_for_flag( *fired ) );
}

// ---------------------------------------------------------------------------
// Tests: acquire fires after release
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore: fires after release from another thread",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 0 );
    asio_semaphore_runner runner;

    auto fired        = std::make_shared< std::atomic< bool > >( false );
    auto release_sent = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_acquire( runner.ioc, sem, [ fired, release_sent ]( auto result ) {
        REQUIRE( result.has_value() );
        REQUIRE( release_sent->load() );
        *fired = true;
    } );

    std::this_thread::sleep_for( 50ms );
    REQUIRE( !fired->load() );

    *release_sent = true;
    sem.release();

    REQUIRE( wait_for_flag( *fired ) );
}

// ---------------------------------------------------------------------------
// Tests: no early wakeup
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore: no early wakeup while not released",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 0 );
    asio_semaphore_runner runner;

    auto fired = std::make_shared< std::atomic< bool > >( false );

    nova::sync::async_acquire( runner.ioc, sem, [ fired ]( auto result ) {
        REQUIRE( result.has_value() );
        *fired = true;
    } );

    std::this_thread::sleep_for( 100ms );
    REQUIRE( !fired->load() );

    sem.release();
    REQUIRE( wait_for_flag( *fired ) );
}

// ---------------------------------------------------------------------------
// Tests: future-based API
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore: future-based async_acquire",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 0 );
    asio_semaphore_runner runner;

    auto state = nova::sync::async_acquire( runner.ioc, sem );

    sem.release();

    auto status = state.future.wait_for( 10s );
    REQUIRE( status == std::future_status::ready );

    runner.stop();
}

// ---------------------------------------------------------------------------
// Tests: cancellation
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore: async_acquire_cancellable - cancel stops acquire",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 0 );
    asio_semaphore_runner runner;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    auto handle = nova::sync::async_acquire_cancellable( runner.ioc, sem, [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    REQUIRE( wait_for_flag( *handler_invoked ) );
}

TEMPLATE_TEST_CASE( "async_semaphore: async_acquire_cancellable - destructor auto-cancels",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 0 );
    asio_semaphore_runner runner;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    {
        auto handle = nova::sync::async_acquire_cancellable( runner.ioc, sem, [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
        // handle destructs here — should auto-cancel
    }

    REQUIRE( wait_for_flag( *handler_invoked ) );
}

// ---------------------------------------------------------------------------
// Tests: multiple concurrent acquires
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_semaphore: multiple acquires each consume one token",
                    "[async_semaphore][boost_asio]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    Sem                   sem( 0 );
    asio_semaphore_runner runner;

    const int tasks       = 4;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i ) {
        nova::sync::async_acquire( runner.ioc, sem, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );
    }

    std::this_thread::sleep_for( 50ms );
    REQUIRE( completions->load() == 0 );

    // Release tokens one at a time
    for ( int i = 0; i < tasks; ++i )
        sem.release();

    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ( completions->load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();
    REQUIRE( completions->load() == tasks );
}

// =============================================================================
// Stress tests
// =============================================================================

TEMPLATE_TEST_CASE( "async_semaphore stress: release races async_acquire",
                    "[async_semaphore][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_SEMAPHORE_TYPES )
{
    using Sem = TestType;
    asio_semaphore_runner runner;

    const int rounds      = 100;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        Sem sem( 0 );

        nova::sync::async_acquire( runner.ioc, sem, [ completions ]( auto result ) {
            REQUIRE( result.has_value() );
            ++( *completions );
        } );

        sem.release();

        std::this_thread::sleep_for( 1ms );
    }

    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ( completions->load() < rounds && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();
    REQUIRE( completions->load() == rounds );
}
