// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/support/boost_asio_support.hpp>

#include "mutex_types.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helper: run an io_context on a background thread.
// ---------------------------------------------------------------------------

struct asio_runner
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

    ~asio_runner()
    {
        stop();
    }
};

// ---------------------------------------------------------------------------
// Context configuration
// ---------------------------------------------------------------------------

#include <nova/sync/mutex/support/boost_asio_support.hpp>

// ---------------------------------------------------------------------------
// Boost.Asio tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_mutex: explicit expected types", "[async_mutex][boost_asio]", NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

#ifdef NOVA_SYNC_HAS_TL_EXPECTED
    SECTION( "tl::expected" )
    {
        nova::sync::async_acquire( runner.ioc, mtx, []( tl::expected< std::unique_lock< Mtx >, std::error_code > result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
        } );
    }
#endif

#ifdef NOVA_SYNC_HAS_STD_EXPECTED
    SECTION( "std::expected" )
    {
        nova::sync::async_acquire( runner.ioc,
                                   mtx,
                                   []( std::expected< std::unique_lock< Mtx >, std::error_code > result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
        } );
    }
#endif
}

TEMPLATE_TEST_CASE( "async_mutex: async acquire fires after unlock",
                    "[async_mutex][boost_asio]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

    auto acquired_after_release = std::make_shared< std::atomic< bool > >( false );
    auto mutex_released         = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    nova::sync::async_acquire( runner.ioc, mtx, [ acquired_after_release ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
    } );

    // Keep the mutex locked for a while so we can observe the handler doesn't
    // fire prematurely.
    nova::sync::async_acquire( runner.ioc, mtx, [ acquired_after_release, mutex_released ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
        *acquired_after_release = mutex_released->load();
    } );

    REQUIRE( !*mutex_released );

    std::this_thread::sleep_for( 30ms );
    *mutex_released = true;
    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !acquired_after_release->load() && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( acquired_after_release->load() );

    mtx.lock(); // verify mutex is free
    mtx.unlock();
}


TEMPLATE_TEST_CASE( "async_mutex: no early wakeup while locked", "[async_mutex][boost_asio]", NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

    auto handler_fired = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    nova::sync::async_acquire( runner.ioc, mtx, [ handler_fired ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
        *handler_fired = true;
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !handler_fired->load() );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !*handler_fired && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( handler_fired->load() );

    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "async_mutex: mutual exclusion with async acquires",
                    "[async_mutex][boost_asio]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

    const int tasks          = 8;
    auto      inside         = std::make_shared< std::atomic< int > >( 0 );
    auto      max_concurrent = std::make_shared< std::atomic< int > >( 0 );
    auto      completions    = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i )
        boost::asio::post( runner.ioc, [ &mtx, &runner, inside, max_concurrent, completions ] {
            nova::sync::async_acquire( runner.ioc, mtx, [ inside, max_concurrent, completions ]( auto result ) {
                REQUIRE( result );
                REQUIRE( result->owns_lock() );
                int current  = ++( *inside );
                int expected = max_concurrent->load();
                while ( current > expected && !max_concurrent->compare_exchange_weak( expected, current ) )
                    ;
                std::this_thread::sleep_for( 1ms );
                --( *inside );
                ++( *completions );
            } );
        } );

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions->load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( completions->load() == tasks );
    REQUIRE( *inside == 0 );
    REQUIRE( *max_concurrent == 1 );
}

TEMPLATE_TEST_CASE( "async_mutex: async acquire future", "[async_mutex][boost_asio]", NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

    mtx.lock();

    auto [ descriptor, fut ] = nova::sync::async_acquire( runner.ioc, mtx );

    mtx.unlock();

    auto lock = fut.get();
    REQUIRE( lock.owns_lock() );

    runner.stop();
}

TEMPLATE_TEST_CASE( "async_acquire_cancellable — cancel prevents handler",
                    "[async_mutex][boost_asio]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    auto handle = nova::sync::async_acquire_cancellable( runner.ioc, mtx, [ handler_invoked ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        *handler_invoked = true;
    } );

    handle.cancel();

    // Give time for handler to fire (it should fire with cancellation)
    std::this_thread::sleep_for( 100ms );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_invoked->load() && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( handler_invoked->load() );
}

TEMPLATE_TEST_CASE( "async_acquire_cancellable — destructor auto-cancels",
                    "[async_mutex][boost_asio]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx         mtx;
    asio_runner runner;

    auto handler_invoked = std::make_shared< std::atomic< bool > >( false );

    mtx.lock();

    {
        auto handle = nova::sync::async_acquire_cancellable( runner.ioc, mtx, [ handler_invoked ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            *handler_invoked = true;
        } );
    }

    // Give time for handler to fire
    std::this_thread::sleep_for( 100ms );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_invoked->load() && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( handler_invoked->load() );
}

// ---------------------------------------------------------------------------
// Stress / edge-case tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "async_mutex stress: high contention many async waiters",
                    "[async_mutex][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;

    // Use a larger thread pool to increase contention
    boost::asio::io_context                                                    ioc;
    boost::asio::executor_work_guard< boost::asio::io_context::executor_type > work_guard {
        boost::asio::make_work_guard( ioc ) };

    const int                  thread_count = 4;
    std::vector< std::thread > threads;
    threads.reserve( thread_count );
    for ( int i = 0; i < thread_count; ++i )
        threads.emplace_back( [ &ioc ] {
            ioc.run();
        } );

    Mtx       mtx;
    const int tasks          = 32;
    auto      inside         = std::make_shared< std::atomic< int > >( 0 );
    auto      max_concurrent = std::make_shared< std::atomic< int > >( 0 );
    auto      completions    = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < tasks; ++i )
        boost::asio::post( ioc, [ &mtx, &ioc, inside, max_concurrent, completions ] {
            nova::sync::async_acquire( ioc, mtx, [ inside, max_concurrent, completions ]( auto result ) {
                REQUIRE( result );
                REQUIRE( result->owns_lock() );
                int current  = ++( *inside );
                int expected = max_concurrent->load();
                while ( current > expected && !max_concurrent->compare_exchange_weak( expected, current ) )
                    ;
                // Small pause to increase chance of detecting concurrent acquisition
                std::this_thread::sleep_for( 100us );
                --( *inside );
                ++( *completions );
            } );
        } );

    auto deadline = std::chrono::steady_clock::now() + 10s;
    while ( completions->load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    work_guard.reset();
    for ( auto& t : threads )
        if ( t.joinable() )
            t.join();

    REQUIRE( completions->load() == tasks );
    REQUIRE( *inside == 0 );
    REQUIRE( *max_concurrent == 1 );

    // Mutex must be acquirable after all async ops complete (no stray notifications)
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "async_mutex stress: unlock races CAS acquisition",
                    "[async_mutex][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // Repeatedly lock from one thread, start async waiter, then immediately
    // unlock — exercising the race between unlock() and the async wakeup path.
    using Mtx = TestType;

    asio_runner runner;
    Mtx         mtx;

    const int rounds      = 50;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        mtx.lock();

        nova::sync::async_acquire( runner.ioc, mtx, [ completions ]( auto result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
            ++( *completions );
        } );

        // Unlock immediately — races with the async wakeup
        mtx.unlock();
    }

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions->load() < rounds && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( completions->load() == rounds );

    // Mutex must be cleanly acquirable with no stray notifications
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "async_mutex stress: cancellation under load",
                    "[async_mutex][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // Start many async acquires; cancel some immediately after registering.
    // The non-cancelled ones should eventually succeed after the lock is released.
    using Mtx = TestType;

    asio_runner runner;
    Mtx         mtx;
    mtx.lock(); // hold the lock so all waiters block

    const int total     = 16;
    const int to_cancel = total / 2;
    auto      successes = std::make_shared< std::atomic< int > >( 0 );
    auto      cancels   = std::make_shared< std::atomic< int > >( 0 );

    // Post the non-cancellable ones
    for ( int i = 0; i < ( total - to_cancel ); ++i ) {
        nova::sync::async_acquire( runner.ioc, mtx, [ successes ]( auto result ) {
            if ( result )
                ++( *successes );
        } );
    }

    // Post cancellable ones and cancel them immediately
    for ( int i = 0; i < to_cancel; ++i ) {
        auto handle = nova::sync::async_acquire_cancellable( runner.ioc, mtx, [ cancels ]( auto result ) {
            if ( !result )
                ++( *cancels );
        } );
        handle.cancel(); // cancel right away; handle destructs here
    }

    // Now release the lock so the remaining waiters can proceed
    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( ( successes->load() + cancels->load() ) < total && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( successes->load() + cancels->load() == total );
    REQUIRE( successes->load() >= 1 );
}

TEMPLATE_TEST_CASE( "async_mutex stress: no stray notifications after acquire cycles",
                    "[async_mutex][boost_asio][stress]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // After repeated lock/unlock/async-acquire cycles the mutex should have
    // no pending notifications and be immediately acquirable with try_lock().
    using Mtx = TestType;

    asio_runner runner;
    Mtx         mtx;

    const int rounds      = 20;
    auto      completions = std::make_shared< std::atomic< int > >( 0 );

    for ( int i = 0; i < rounds; ++i ) {
        auto promise = std::make_shared< std::promise< void > >();
        auto fut     = promise->get_future();

        mtx.lock();
        nova::sync::async_acquire( runner.ioc, mtx, [ completions, promise ]( auto result ) {
            REQUIRE( result );
            ++( *completions );
            promise->set_value();
        } );
        mtx.unlock();

        // Wait for this round to complete before starting the next
        fut.wait_for( 2s );
    }

    runner.stop();

    REQUIRE( completions->load() == rounds );

    // Final check: no stray notifications — try_lock must succeed
    mtx.lock(); // verify mutex is free
    mtx.unlock();
}
