// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

// libdispatch tests (Objective-C++ only)

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/async_concepts.hpp>
#include <nova/sync/mutex/support/libdispatch_support.hpp>

#include "mutex_types.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// libdispatch tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "native_async_mutex: explicit expected types",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > handler_fired { false };

#ifdef NOVA_SYNC_HAS_TL_EXPECTED
    SECTION( "tl::expected" )
    {
        nova::sync::async_acquire( mtx, queue, [ & ]( tl::expected< std::unique_lock< Mtx >, std::error_code > result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
            handler_fired = true;
        } );

        auto deadline = std::chrono::steady_clock::now() + 2s;
        while ( !handler_fired && std::chrono::steady_clock::now() < deadline )
            std::this_thread::sleep_for( 5ms );

        REQUIRE( handler_fired.load() );
    }
#endif

#ifdef NOVA_SYNC_HAS_STD_EXPECTED
    SECTION( "std::expected" )
    {
        nova::sync::async_acquire( mtx, queue, [ & ]( std::expected< std::unique_lock< Mtx >, std::error_code > result ) {
            REQUIRE( result );
            REQUIRE( result->owns_lock() );
            handler_fired = true;
        } );

        auto deadline = std::chrono::steady_clock::now() + 2s;
        while ( !handler_fired && std::chrono::steady_clock::now() < deadline )
            std::this_thread::sleep_for( 5ms );

        REQUIRE( handler_fired.load() );
    }
#endif

    nova::sync::detail::release_dispatch_object( queue );
}

TEMPLATE_TEST_CASE( "libdispatch: async acquire fires after unlock",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > handler_fired { false };

    mtx.lock();

    nova::sync::async_acquire( mtx, queue, [ & ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
        handler_fired = true;
    } );

    REQUIRE( !handler_fired );
    std::this_thread::sleep_for( 30ms );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_fired && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    nova::sync::detail::release_dispatch_object( queue );

    REQUIRE( handler_fired.load() );

    REQUIRE( mtx.try_lock() );
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "libdispatch: no early wakeup while locked",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > handler_fired { false };

    mtx.lock();

    nova::sync::async_acquire( mtx, queue, [ & ]( auto result ) {
        REQUIRE( result );
        REQUIRE( result->owns_lock() );
        handler_fired = true;
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !handler_fired.load() );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_fired && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    nova::sync::detail::release_dispatch_object( queue );

    REQUIRE( handler_fired.load() );

    REQUIRE( mtx.try_lock() );
    mtx.unlock();
}

TEMPLATE_TEST_CASE( "libdispatch: mutual exclusion with async acquires",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    const int          tasks = 8;
    std::atomic< int > inside { 0 };
    std::atomic< int > max_concurrent { 0 };
    std::atomic< int > completions { 0 };

    for ( int i = 0; i < tasks; ++i )
        dispatch_async( queue, [ & ] {
            nova::sync::async_acquire( mtx, queue, [ & ]( auto result ) {
                REQUIRE( result );
                REQUIRE( result->owns_lock() );
                int current  = ++inside;
                int expected = max_concurrent.load();
                while ( current > expected && !max_concurrent.compare_exchange_weak( expected, current ) )
                    ;
                std::this_thread::sleep_for( 1ms );
                --inside;
                ++completions;
            } );
        } );

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    nova::sync::detail::release_dispatch_object( queue );

    REQUIRE( completions.load() == tasks );
    REQUIRE( inside == 0 );
    REQUIRE( max_concurrent == 1 );
}

TEMPLATE_TEST_CASE( "libdispatch — cancellation delivers error to handler",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > handler_invoked { false };

    mtx.lock();

    auto handle = nova::sync::async_acquire_cancellable( mtx, queue, [ & ]( auto result ) {
        REQUIRE( !result );
        REQUIRE( result.error() == std::errc::operation_canceled );
        handler_invoked = true;
    } );

    handle.cancel();

    // Give time for handler to fire
    std::this_thread::sleep_for( 100ms );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_invoked && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    nova::sync::detail::release_dispatch_object( queue );

    REQUIRE( handler_invoked.load() );
}

TEMPLATE_TEST_CASE( "libdispatch — destructor auto-cancels",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > handler_invoked { false };

    mtx.lock();

    {
        auto handle = nova::sync::async_acquire_cancellable( mtx, queue, [ & ]( auto result ) {
            REQUIRE( !result );
            REQUIRE( result.error() == std::errc::operation_canceled );
            handler_invoked = true;
        } );
        // Handle goes out of scope here — destructor auto-cancels
    }

    // Give time for handler to fire
    std::this_thread::sleep_for( 100ms );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_invoked && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    nova::sync::detail::release_dispatch_object( queue );

    REQUIRE( handler_invoked.load() );
}

TEMPLATE_TEST_CASE( "libdispatch: future becomes ready after unlock",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    mtx.lock();

    auto state = nova::sync::async_acquire( mtx, queue );

    // Future should not be ready while locked
    REQUIRE( state.future.wait_for( 30ms ) == std::future_status::timeout );

    mtx.unlock();

    // Future should become ready after unlock
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( state.future.wait_for( 5ms ) == std::future_status::timeout && std::chrono::steady_clock::now() < deadline )
        ;

    REQUIRE( state.future.wait_for( 0ms ) == std::future_status::ready );
    auto lock = state.future.get();
    REQUIRE( lock.owns_lock() );

    nova::sync::detail::release_dispatch_object( queue );
}

TEMPLATE_TEST_CASE( "libdispatch: future ready immediately if mutex available",
                    "[native_async_mutex][libdispatch]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    using Mtx = TestType;
    Mtx              mtx;
    dispatch_queue_t queue = dispatch_queue_create( "test_queue", DISPATCH_QUEUE_SERIAL );

    // Mutex is immediately available
    auto state = nova::sync::async_acquire( mtx, queue );

    // Future should already be ready
    REQUIRE( state.future.wait_for( 0ms ) == std::future_status::ready );
    auto lock = state.future.get();
    REQUIRE( lock.owns_lock() );

    nova::sync::detail::release_dispatch_object( queue );
}
