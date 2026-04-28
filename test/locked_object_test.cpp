// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/thread_safety/locked_object.hpp>

#include "mutex_types.hpp"

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ===========================================================================
// lock_guard proxy tests
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: lock returns guard with access",
                    "[locked_object]",
                    std::mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 42 );

    SECTION( "dereference via operator*" )
    {
        auto guard = obj.lock();
        REQUIRE( *guard == 42 );
        *guard = 99;
        REQUIRE( *guard == 99 );
    }

    SECTION( "dereference via operator->" )
    {
        nova::sync::locked_object< std::string, TestType > sobj( "hello" );
        auto                                               guard = sobj.lock();
        REQUIRE( guard->size() == 5 );
        guard->append( " world" );
        REQUIRE( *guard == "hello world" );
    }

    SECTION( "get() returns raw pointer" )
    {
        auto guard = obj.lock();
        REQUIRE( guard.get() != nullptr );
        REQUIRE( *guard.get() == 42 );
    }
}

// ===========================================================================
// Guard is move-only
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: guard is movable", "[locked_object]", std::mutex, NOVA_SYNC_ALL_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 7 );

    auto guard1 = obj.lock();
    REQUIRE( *guard1 == 7 );

    auto guard2 = std::move( guard1 );
    REQUIRE( *guard2 == 7 );
    // guard1 moved-from — destruction must not double-unlock
}

// ===========================================================================
// try_lock
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: try_lock", "[locked_object]", std::mutex, NOVA_SYNC_ALL_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 10 );

    SECTION( "succeeds when uncontended" )
    {
        auto guard = obj.try_lock();
        REQUIRE( guard.has_value() );
        REQUIRE( **guard == 10 );
    }

    SECTION( "fails when already locked" )
    {
        if constexpr ( !nova::sync::concepts::recursive_mutex< TestType > ) {
            auto guard  = obj.lock();
            auto guard2 = obj.try_lock();
            REQUIRE( !guard2.has_value() );
        }
    }
}

// ===========================================================================
// lock_and higher-order function
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: lock_and", "[locked_object]", std::mutex, NOVA_SYNC_ALL_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 5 );

    SECTION( "returns value from callable" )
    {
        int result = obj.lock_and( []( int& v ) {
            return v * 2;
        } );
        REQUIRE( result == 10 );
    }

    SECTION( "mutates guarded value" )
    {
        obj.lock_and( []( int& v ) {
            v = 42;
        } );
        auto guard = obj.lock();
        REQUIRE( *guard == 42 );
    }

    SECTION( "void return" )
    {
        obj.lock_and( []( int& v ) {
            v += 1;
        } );
        REQUIRE( *obj.lock() == 6 );
    }
}

// ===========================================================================
// try_lock_and higher-order function
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: try_lock_and", "[locked_object]", std::mutex, NOVA_SYNC_ALL_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 100 );

    SECTION( "succeeds when uncontended" )
    {
        auto result = obj.try_lock_and( []( int& v ) {
            return v + 1;
        } );
        REQUIRE( result.has_value() );
        REQUIRE( *result == 101 );
    }

    SECTION( "void return when uncontended" )
    {
        auto result = obj.try_lock_and( []( int& v ) {
            v = 0;
        } );
        REQUIRE( result.has_value() );
    }

    SECTION( "fails when already locked" )
    {
        if constexpr ( !nova::sync::concepts::recursive_mutex< TestType > ) {
            auto guard  = obj.lock();
            auto result = obj.try_lock_and( []( int& v ) {
                return v;
            } );
            REQUIRE( !result.has_value() );
        }
    }
}

// ===========================================================================
// Mutual exclusion across threads
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: mutual exclusion", "[locked_object][stress]", std::mutex, NOVA_SYNC_ALL_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 0 );

    const unsigned threads    = std::max( 2u, std::thread::hardware_concurrency() );
    const unsigned iterations = 5000u;

    std::vector< std::thread > ths;
    ths.reserve( threads );

    for ( unsigned t = 0; t < threads; ++t ) {
        ths.emplace_back( [ & ] {
            for ( unsigned i = 0; i < iterations; ++i ) {
                obj.lock_and( []( int& v ) {
                    ++v;
                } );
            }
        } );
    }

    for ( auto& t : ths )
        t.join();

    REQUIRE( *obj.lock() == int( threads * iterations ) );
}

// ===========================================================================
// Default construction
// ===========================================================================

TEST_CASE( "locked_object: default construction", "[locked_object]" )
{
    nova::sync::locked_object< int > obj;
    REQUIRE( *obj.lock() == 0 );
}

// ===========================================================================
// Multi-arg construction
// ===========================================================================

TEST_CASE( "locked_object: multi-arg construction", "[locked_object]" )
{
    nova::sync::locked_object< std::string > obj( 5u, 'x' );
    REQUIRE( *obj.lock() == "xxxxx" );
}

// ===========================================================================
// Shared locking
// ===========================================================================

TEST_CASE( "locked_object: shared locking with std::shared_mutex", "[locked_object]" )
{
    nova::sync::locked_object< int, std::shared_mutex > obj( 42 );

    SECTION( "lock_shared provides const access" )
    {
        auto guard = obj.lock_shared();
        REQUIRE( *guard == 42 );
        // *guard = 99;  // should not compile — const access only
    }

    SECTION( "try_lock_shared succeeds" )
    {
        auto guard = obj.try_lock_shared();
        REQUIRE( guard.has_value() );
        REQUIRE( **guard == 42 );
    }

    SECTION( "multiple shared locks simultaneously" )
    {
        auto g1 = obj.lock_shared();
        auto g2 = obj.try_lock_shared();
        REQUIRE( g2.has_value() );
        REQUIRE( *g1 == **g2 );
    }

    SECTION( "exclusive lock blocked while shared held" )
    {
        auto shared = obj.lock_shared();
        auto excl   = obj.try_lock();
        REQUIRE( !excl.has_value() );
    }
}

// ===========================================================================
// Shared locking — lock_shared_and / try_lock_shared_and
// ===========================================================================

TEST_CASE( "locked_object: lock_shared_and", "[locked_object]" )
{
    nova::sync::locked_object< int, std::shared_mutex > obj( 10 );

    SECTION( "returns value" )
    {
        int result = obj.lock_shared_and( []( const int& v ) {
            return v * 3;
        } );
        REQUIRE( result == 30 );
    }

    SECTION( "void return" )
    {
        int captured = 0;
        obj.lock_shared_and( [ &captured ]( const int& v ) {
            captured = v;
        } );
        REQUIRE( captured == 10 );
    }
}

TEST_CASE( "locked_object: try_lock_shared_and", "[locked_object]" )
{
    nova::sync::locked_object< int, std::shared_mutex > obj( 20 );

    SECTION( "succeeds when uncontended" )
    {
        auto result = obj.try_lock_shared_and( []( const int& v ) {
            return v + 5;
        } );
        REQUIRE( result.has_value() );
        REQUIRE( *result == 25 );
    }
}

// ===========================================================================
// Shared locking — concurrent readers
// ===========================================================================

TEST_CASE( "locked_object: concurrent shared readers", "[locked_object][stress]" )
{
    nova::sync::locked_object< int, std::shared_mutex > obj( 42 );

    const unsigned             threads = std::max( 2u, std::thread::hardware_concurrency() );
    std::atomic< int >         sum { 0 };
    std::vector< std::thread > ths;
    ths.reserve( threads );

    for ( unsigned t = 0; t < threads; ++t ) {
        ths.emplace_back( [ & ] {
            for ( int i = 0; i < 1000; ++i ) {
                obj.lock_shared_and( [ & ]( const int& v ) {
                    sum += v;
                } );
            }
        } );
    }

    for ( auto& t : ths )
        t.join();

    REQUIRE( sum.load() == int( threads * 1000 * 42 ) );
}

// ===========================================================================
// Shared locking with nova types
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: shared locking with nova shared mutex",
                    "[locked_object]",
                    NOVA_SYNC_SHARED_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 55 );

    SECTION( "lock_shared provides const access" )
    {
        auto guard = obj.lock_shared();
        REQUIRE( *guard == 55 );
    }

    SECTION( "try_lock_shared succeeds" )
    {
        auto guard = obj.try_lock_shared();
        REQUIRE( guard.has_value() );
        REQUIRE( **guard == 55 );
    }

    SECTION( "lock_shared_and returns value" )
    {
        int result = obj.lock_shared_and( []( const int& v ) {
            return v + 1;
        } );
        REQUIRE( result == 56 );
    }
}

// ===========================================================================
// Timed locking
// ===========================================================================

#ifdef NOVA_SYNC_TIMED_MUTEX_TYPES

TEMPLATE_TEST_CASE( "locked_object: timed locking", "[locked_object]", NOVA_SYNC_TIMED_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 77 );

    SECTION( "try_lock_for succeeds when uncontended" )
    {
        auto guard = obj.try_lock_for( 100ms );
        REQUIRE( guard.has_value() );
        REQUIRE( **guard == 77 );
    }

    SECTION( "try_lock_for times out when locked" )
    {
        auto guard = obj.lock();

        bool        acquired = false;
        std::thread other( [ & ] {
            auto g = obj.try_lock_for( 30ms );
            if ( g.has_value() )
                acquired = true;
        } );

        other.join();
        REQUIRE( !acquired );
    }

    SECTION( "try_lock_until succeeds when uncontended" )
    {
        auto guard = obj.try_lock_until( std::chrono::steady_clock::now() + 100ms );
        REQUIRE( guard.has_value() );
        REQUIRE( **guard == 77 );
    }

    SECTION( "try_lock_until times out when locked" )
    {
        auto guard = obj.lock();

        bool        acquired = false;
        std::thread other( [ & ] {
            auto g = obj.try_lock_until( std::chrono::steady_clock::now() + 30ms );
            if ( g.has_value() )
                acquired = true;
        } );

        other.join();
        REQUIRE( !acquired );
    }

    SECTION( "try_lock_for_and succeeds" )
    {
        auto result = obj.try_lock_for_and( 100ms, []( int& v ) {
            return v * 2;
        } );
        REQUIRE( result.has_value() );
        REQUIRE( *result == 154 );
    }

    SECTION( "try_lock_for_and times out" )
    {
        auto guard  = obj.lock();
        auto result = std::optional< int > {};

        std::thread other( [ & ] {
            result = obj.try_lock_for_and( 30ms, []( int& v ) {
                return v;
            } );
        } );

        other.join();
        REQUIRE( !result.has_value() );
    }
}

#endif // NOVA_SYNC_TIMED_MUTEX_TYPES

// ===========================================================================
// Recursive locking
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: recursive locking", "[locked_object]", NOVA_SYNC_RECURSIVE_MUTEX_TYPES )
{
    nova::sync::locked_object< int, TestType > obj( 1 );

    SECTION( "can lock recursively" )
    {
        auto g1 = obj.lock();
        auto g2 = obj.try_lock();
        REQUIRE( g2.has_value() );
        **g2 = 2;
        REQUIRE( *g1 == 2 );
    }

    SECTION( "lock_and inside lock_and" )
    {
        obj.lock_and( [ & ]( int& v ) {
            v = 10;
            obj.lock_and( [ & ]( int& v2 ) {
                v2 = 20;
            } );
        } );
        REQUIRE( *obj.lock() == 20 );
    }
}

// ===========================================================================
// Complex value type
// ===========================================================================

TEST_CASE( "locked_object: complex value type", "[locked_object]" )
{
    struct config
    {
        std::string name;
        int         value;
    };

    nova::sync::locked_object< config > obj( config { "test", 42 } );

    SECTION( "access members via operator->" )
    {
        auto guard = obj.lock();
        REQUIRE( guard->name == "test" );
        REQUIRE( guard->value == 42 );
        guard->value = 100;
        REQUIRE( guard->value == 100 );
    }

    SECTION( "lock_and with structured binding" )
    {
        auto name = obj.lock_and( []( config& c ) {
            return c.name;
        } );
        REQUIRE( name == "test" );
    }
}

// ===========================================================================
// const locked_object — exclusive lock, locked_object_guard<const T, M>, const access
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: const instance - lock returns locked_object_guard<const T, M>",
                    "[locked_object]",
                    std::mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    const nova::sync::locked_object< int, TestType > obj( 42 );

    SECTION( "lock() yields locked_object_guard<const T, M> with const access" )
    {
        auto guard = obj.lock();
        // static-assert guard gives const T& (would not compile if mutable)
        static_assert( std::is_same_v< decltype( *guard ), const int& > );
        REQUIRE( *guard == 42 );
    }

    SECTION( "operator-> yields const pointer" )
    {
        const nova::sync::locked_object< std::string, TestType > sobj( "hello" );
        auto                                                     guard = sobj.lock();
        static_assert( std::is_same_v< decltype( guard.get() ), const std::string* > );
        REQUIRE( guard->size() == 5 );
    }

    SECTION( "get() returns const pointer" )
    {
        auto guard = obj.lock();
        REQUIRE( guard.get() != nullptr );
        REQUIRE( *guard.get() == 42 );
    }

    SECTION( "guard is movable" )
    {
        auto g1 = obj.lock();
        auto g2 = std::move( g1 );
        REQUIRE( *g2 == 42 );
    }
}

TEMPLATE_TEST_CASE( "locked_object: const instance - try_lock", "[locked_object]", std::mutex, NOVA_SYNC_ALL_MUTEX_TYPES )
{
    const nova::sync::locked_object< int, TestType > obj( 10 );

    SECTION( "succeeds when uncontended" )
    {
        auto guard = obj.try_lock();
        REQUIRE( guard.has_value() );
        static_assert( std::is_same_v< decltype( **guard ), const int& > );
        REQUIRE( **guard == 10 );
    }
}

TEMPLATE_TEST_CASE( "locked_object: const instance - lock_and with const T&",
                    "[locked_object]",
                    std::mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    const nova::sync::locked_object< int, TestType > obj( 7 );

    SECTION( "returns value from callable" )
    {
        int result = obj.lock_and( []( const int& v ) {
            return v * 3;
        } );
        REQUIRE( result == 21 );
    }

    SECTION( "void callable" )
    {
        int captured = 0;
        obj.lock_and( [ &captured ]( const int& v ) {
            captured = v;
        } );
        REQUIRE( captured == 7 );
    }

    SECTION( "callable receives const ref — static_assert" )
    {
        obj.lock_and( []( const int& v ) {
            static_assert( std::is_same_v< decltype( v ), const int& > );
            (void)v;
        } );
    }
}

TEMPLATE_TEST_CASE( "locked_object: const instance - try_lock_and",
                    "[locked_object]",
                    std::mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    const nova::sync::locked_object< int, TestType > obj( 5 );

    SECTION( "succeeds, returns value" )
    {
        auto result = obj.try_lock_and( []( const int& v ) {
            return v + 1;
        } );
        REQUIRE( result.has_value() );
        REQUIRE( *result == 6 );
    }

    SECTION( "void callable succeeds" )
    {
        auto result = obj.try_lock_and( []( const int& ) {} );
        REQUIRE( result.has_value() );
    }

    SECTION( "fails when already locked" )
    {
        if constexpr ( !nova::sync::concepts::recursive_mutex< TestType > ) {
            auto guard  = obj.lock(); // const lock — locked_object_guard<const T, M>, but still exclusive
            auto result = obj.try_lock_and( []( const int& v ) {
                return v;
            } );
            REQUIRE( !result.has_value() );
        }
    }
}

// ===========================================================================
// const locked_object — mutual exclusion (exclusive lock still blocks writers)
// ===========================================================================

TEMPLATE_TEST_CASE( "locked_object: const instance - mutual exclusion",
                    "[locked_object][stress]",
                    std::mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    // Even though access is read-only, the exclusive lock must still serialize.
    const nova::sync::locked_object< int, TestType > obj( 0 );

    const unsigned             threads    = std::max( 2u, std::thread::hardware_concurrency() );
    const unsigned             iterations = 5000u;
    std::atomic< int >         counter { 0 };
    std::vector< std::thread > ths;
    ths.reserve( threads );

    for ( unsigned t = 0; t < threads; ++t ) {
        ths.emplace_back( [ & ] {
            for ( unsigned i = 0; i < iterations; ++i ) {
                obj.lock_and( [ & ]( const int& ) {
                    ++counter;
                } );
            }
        } );
    }

    for ( auto& t : ths )
        t.join();

    REQUIRE( counter.load() == int( threads * iterations ) );
}

// ===========================================================================
// const locked_object — timed locking
// ===========================================================================

#ifdef NOVA_SYNC_TIMED_MUTEX_TYPES

TEMPLATE_TEST_CASE( "locked_object: const instance - timed locking", "[locked_object]", NOVA_SYNC_TIMED_MUTEX_TYPES )
{
    const nova::sync::locked_object< int, TestType > obj( 77 );

    SECTION( "try_lock_for succeeds when uncontended" )
    {
        auto guard = obj.try_lock_for( 100ms );
        REQUIRE( guard.has_value() );
        static_assert( std::is_same_v< decltype( **guard ), const int& > );
        REQUIRE( **guard == 77 );
    }

    SECTION( "try_lock_until succeeds when uncontended" )
    {
        auto guard = obj.try_lock_until( std::chrono::steady_clock::now() + 100ms );
        REQUIRE( guard.has_value() );
        REQUIRE( **guard == 77 );
    }

    SECTION( "try_lock_for times out when locked" )
    {
        auto guard = obj.lock();

        bool        acquired = false;
        std::thread other( [ & ] {
            auto g = obj.try_lock_for( 30ms );
            if ( g.has_value() )
                acquired = true;
        } );

        other.join();
        REQUIRE( !acquired );
    }

    SECTION( "try_lock_for_and succeeds" )
    {
        auto result = obj.try_lock_for_and( 100ms, []( const int& v ) {
            return v * 2;
        } );
        REQUIRE( result.has_value() );
        REQUIRE( *result == 154 );
    }
}

#endif // NOVA_SYNC_TIMED_MUTEX_TYPES
