// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/async_concepts.hpp>
#include <nova/sync/mutex/native_async_mutex.hpp>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;
using Mtx = nova::sync::native_async_mutex;

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

#include <nova/sync/mutex/boost_asio_support.hpp>

// ---------------------------------------------------------------------------
// Boost.Asio tests
// ---------------------------------------------------------------------------

TEST_CASE( "native_async_mutex: explicit expected types", "[native_async_mutex][boost_asio]" )
{
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

TEST_CASE( "native_async_mutex: async acquire fires after unlock", "[native_async_mutex][boost_asio]" )
{
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

    REQUIRE( mtx.try_lock() );
    mtx.unlock();
}


TEST_CASE( "native_async_mutex: no early wakeup while locked", "[native_async_mutex][boost_asio]" )
{
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

    REQUIRE( mtx.try_lock() );
    mtx.unlock();
}

TEST_CASE( "native_async_mutex: mutual exclusion with async acquires", "[native_async_mutex][boost_asio]" )
{
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

TEST_CASE( "native_async_mutex: concepts", "[native_async_mutex][boost_asio]" )
{
    // The concept checks that handlers accepting expected<unique_lock, error_code>
    // are properly recognized. This is tested at runtime in the other test cases.
    // The concept is implicitly checked when calling async_acquire with such handlers.
}

TEST_CASE( "native_async_mutex: async acquire future", "[native_async_mutex][boost_asio]" )
{
    Mtx         mtx;
    asio_runner runner;

    mtx.lock();

    auto [ descriptor, fut ] = nova::sync::async_acquire( runner.ioc, mtx );

    mtx.unlock();

    auto lock = fut.get();
    REQUIRE( lock.owns_lock() );

    runner.stop();
}

TEST_CASE( "async_acquire_cancellable — cancel prevents handler", "[native_async_mutex][boost_asio]" )
{
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

TEST_CASE( "async_acquire_cancellable — destructor auto-cancels", "[native_async_mutex][boost_asio]" )
{
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
