// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/futex/atomic_wait.hpp>

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// =============================================================================
// Basic correctness
// =============================================================================

TEST_CASE( "atomic_wait_for returns immediately when value differs", "[futex]" )
{
    std::atomic< int32_t > a { 42 };
    // old != current → should return true instantly
    bool                   ok = nova::sync::atomic_wait_for( a, 0, 100ms );
    REQUIRE( ok );
}

TEST_CASE( "atomic_wait_for times out when value matches", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    auto                   t0      = std::chrono::steady_clock::now();
    bool                   ok      = nova::sync::atomic_wait_for( a, 0, 30ms );
    auto                   elapsed = std::chrono::steady_clock::now() - t0;

    REQUIRE( !ok );
    // Should have waited at least ~30ms (allow 15ms slack for scheduling)
    REQUIRE( elapsed >= 15ms );
}

TEST_CASE( "atomic_wait_for with zero timeout", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    bool                   ok = nova::sync::atomic_wait_for( a, 0, 0ns );
    REQUIRE( !ok );
}

TEST_CASE( "atomic_wait_for with negative timeout", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    bool                   ok = nova::sync::atomic_wait_for( a, 0, -1ms );
    REQUIRE( !ok );
}

// =============================================================================
// Wakeup via notify
// =============================================================================

TEST_CASE( "atomic_wait_for wakes on notify_one", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    std::latch             started { 1 };

    std::thread waiter( [ & ] {
        started.count_down();
        bool ok = nova::sync::atomic_wait_for( a, 0, 2s );
        REQUIRE( ok );
        REQUIRE( a.load() != 0 );
    } );

    started.wait();
    std::this_thread::sleep_for( 30ms );
    a.store( 1, std::memory_order_relaxed );
    nova::sync::atomic_notify_one( a );

    waiter.join();
}

TEST_CASE( "atomic_wait_for wakes on notify_all", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    const unsigned         n = std::max( 2u, std::thread::hardware_concurrency() );
    std::latch             started { static_cast< std::ptrdiff_t >( n ) };
    std::atomic< int >     woken { 0 };

    std::vector< std::thread > threads;
    threads.reserve( n );

    for ( unsigned i = 0; i < n; ++i ) {
        threads.emplace_back( [ & ] {
            started.count_down();
            bool ok = nova::sync::atomic_wait_for( a, 0, 2s );
            if ( ok )
                ++woken;
        } );
    }

    started.wait();
    std::this_thread::sleep_for( 30ms );
    a.store( 1, std::memory_order_relaxed );
    nova::sync::atomic_notify_all( a );

    for ( auto& t : threads )
        t.join();

    REQUIRE( woken.load() == static_cast< int >( n ) );
}

// =============================================================================
// Absolute deadline overloads
// =============================================================================

TEST_CASE( "atomic_wait_until (steady_clock) times out", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    auto                   deadline = std::chrono::steady_clock::now() + 30ms;

    bool ok = nova::sync::atomic_wait_until( a, 0, deadline );
    REQUIRE( !ok );
    REQUIRE( std::chrono::steady_clock::now() >= deadline - 5ms );
}

TEST_CASE( "atomic_wait_until (steady_clock) wakes on notify", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    std::latch             started { 1 };

    std::thread waiter( [ & ] {
        started.count_down();
        auto deadline = std::chrono::steady_clock::now() + 2s;
        bool ok       = nova::sync::atomic_wait_until( a, 0, deadline );
        REQUIRE( ok );
    } );

    started.wait();
    std::this_thread::sleep_for( 30ms );
    a.store( 1, std::memory_order_relaxed );
    nova::sync::atomic_notify_one( a );

    waiter.join();
}

TEST_CASE( "atomic_wait_until (system_clock) times out", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    auto                   deadline = std::chrono::system_clock::now() + 30ms;

    bool ok = nova::sync::atomic_wait_until( a, 0, deadline );
    REQUIRE( !ok );
}

TEST_CASE( "atomic_wait_until (system_clock) wakes on notify", "[futex]" )
{
    std::atomic< int32_t > a { 0 };
    std::latch             started { 1 };

    std::thread waiter( [ & ] {
        started.count_down();
        auto deadline = std::chrono::system_clock::now() + 2s;
        bool ok       = nova::sync::atomic_wait_until( a, 0, deadline );
        REQUIRE( ok );
    } );

    started.wait();
    std::this_thread::sleep_for( 30ms );
    a.store( 1, std::memory_order_relaxed );
    nova::sync::atomic_notify_one( a );

    waiter.join();
}

// =============================================================================
// Stress: multiple waiters with different atomics (no false sharing of buckets)
// =============================================================================

TEST_CASE( "atomic_wait_for stress - independent atomics", "[futex][stress]" )
{
    constexpr int          iterations = 200;
    std::atomic< int32_t > a { 0 };
    std::atomic< int32_t > b { 0 };

    std::thread ta( [ & ] {
        for ( int i = 0; i < iterations; ++i ) {
            while ( a.load( std::memory_order_relaxed ) == i )
                nova::sync::atomic_wait_for( a, i, 100ms );
        }
    } );

    std::thread tb( [ & ] {
        for ( int i = 0; i < iterations; ++i ) {
            while ( b.load( std::memory_order_relaxed ) == i )
                nova::sync::atomic_wait_for( b, i, 100ms );
        }
    } );

    for ( int i = 1; i <= iterations; ++i ) {
        a.store( i, std::memory_order_relaxed );
        nova::sync::atomic_notify_one( a );
        b.store( i, std::memory_order_relaxed );
        nova::sync::atomic_notify_one( b );
    }

    ta.join();
    tb.join();

    REQUIRE( a.load() == iterations );
    REQUIRE( b.load() == iterations );
}

// =============================================================================
// Ping-pong between two threads
// =============================================================================

TEST_CASE( "atomic_wait_for ping-pong", "[futex][stress]" )
{
    constexpr int          rounds = 500;
    std::atomic< int32_t > turn { 0 }; // 0 = thread A's turn, 1 = thread B's turn

    std::thread a( [ & ] {
        for ( int i = 0; i < rounds; ++i ) {
            while ( turn.load( std::memory_order_relaxed ) != 0 )
                nova::sync::atomic_wait_for( turn, 1, 100ms );
            turn.store( 1, std::memory_order_relaxed );
            nova::sync::atomic_notify_one( turn );
        }
    } );

    std::thread b( [ & ] {
        for ( int i = 0; i < rounds; ++i ) {
            while ( turn.load( std::memory_order_relaxed ) != 1 )
                nova::sync::atomic_wait_for( turn, 0, 100ms );
            turn.store( 0, std::memory_order_relaxed );
            nova::sync::atomic_notify_one( turn );
        }
    } );

    a.join();
    b.join();
}

TEST_CASE( "atomic_wait_for: acquire memory ordering with notification", "[futex]" )
{
    // This test verifies that atomic_wait_for with acquire order
    // properly synchronizes memory with the notifier, even on the
    // portable fallback (condvar-based) implementation.

    std::atomic< int32_t > value( 0 );
    std::atomic< int >     shared_data( 0 );

    std::thread writer( [ & ] {
        shared_data.store( 42, std::memory_order_relaxed );
        value.store( 1, std::memory_order_release );
        nova::sync::atomic_notify_one( value );
    } );

    std::thread waiter( [ & ] {
        // Wait with acquire ordering
        nova::sync::atomic_wait_for( value, 0, std::chrono::seconds( 1 ), std::memory_order_acquire );

        // With acquire semantics, we should see the write from writer thread
        int data = shared_data.load( std::memory_order_relaxed );
        REQUIRE( data == 42 ); // Would fail without acquire fence on weak architectures
    } );

    writer.join();
    waiter.join();
}

TEST_CASE( "atomic_wait_until: acquire memory ordering with notification (steady_clock)", "[futex]" )
{
    // Same test but with try_acquire_until overload and steady_clock

    std::atomic< int32_t > value( 0 );
    std::atomic< int >     shared_data( 0 );

    std::thread writer( [ & ] {
        shared_data.store( 99, std::memory_order_relaxed );
        value.store( 1, std::memory_order_release );
        nova::sync::atomic_notify_one( value );
    } );

    std::thread waiter( [ & ] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 1 );
        nova::sync::atomic_wait_until( value, 0, deadline, std::memory_order_acquire );

        // With acquire semantics, we should see the write from writer thread
        int data = shared_data.load( std::memory_order_relaxed );
        REQUIRE( data == 99 );
    } );

    writer.join();
    waiter.join();
}
