// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/auto_reset_event.hpp>
#include <nova/sync/manual_reset_event.hpp>
#include <nova/sync/native_auto_reset_event.hpp>
#include <nova/sync/native_manual_reset_event.hpp>
#include <nova/sync/timed_auto_reset_event.hpp>
#include <nova/sync/timed_manual_reset_event.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// =============================================================================
// Shared helpers
// =============================================================================

/// Simple RAII thread guard that joins on destruction.
struct thread_guard
{
    std::vector< std::thread > threads;
    ~thread_guard()
    {
        for ( auto& t : threads )
            t.join();
    }
};

// =============================================================================
// manual_reset_event variants — correctness tests
// =============================================================================

TEMPLATE_TEST_CASE( "manual_reset_event implementations",
                    "[manual_reset_event]",
                    nova::sync::manual_reset_event,
                    nova::sync::timed_manual_reset_event,
                    nova::sync::native_manual_reset_event )
{
    using event_t = TestType;
    event_t ev;

    SECTION( "initially not set" )
    {
        REQUIRE( !ev.try_wait() );
    }

    SECTION( "initially set via constructor" )
    {
        event_t ev2( true );
        REQUIRE( ev2.try_wait() );
        ev2.wait(); // must not block
    }

    SECTION( "signal / try_wait / reset" )
    {
        ev.signal();
        REQUIRE( ev.try_wait() );
        ev.reset();
        REQUIRE( !ev.try_wait() );
    }

    SECTION( "signal is idempotent" )
    {
        ev.signal();
        ev.signal(); // second call must not throw or misbehave
        REQUIRE( ev.try_wait() );
    }

    SECTION( "wait on already-set event returns immediately" )
    {
        ev.signal();
        ev.wait();                // must not block
        REQUIRE( ev.try_wait() ); // signal is still set (manual reset)
    }

    SECTION( "reset then signal cycle" )
    {
        for ( int i = 0; i < 5; ++i ) {
            REQUIRE( !ev.try_wait() );
            ev.signal();
            REQUIRE( ev.try_wait() );
            ev.reset();
        }
    }

    SECTION( "all waiting threads wake on signal" )
    {
        const unsigned     n = std::max( 2u, std::thread::hardware_concurrency() );
        std::atomic< int > woken { 0 };

        thread_guard guard;
        guard.threads.reserve( n );

        for ( unsigned i = 0; i < n; ++i ) {
            guard.threads.emplace_back( [ & ] {
                ev.wait();
                ++woken;
            } );
        }

        // Give threads time to park before we signal.
        std::this_thread::sleep_for( 30ms );
        REQUIRE( woken.load() == 0 );

        ev.signal();

        for ( auto& t : guard.threads )
            t.join();
        guard.threads.clear();

        REQUIRE( woken.load() == int( n ) );
        // Signal is still latched — further try_wait must succeed.
        REQUIRE( ev.try_wait() );
    }

    SECTION( "reset blocks subsequent waiters" )
    {
        std::atomic< bool > entered { false };

        ev.signal();
        ev.reset();

        std::thread t( [ & ] {
            ev.wait();
            entered.store( true );
        } );

        std::this_thread::sleep_for( 30ms );
        REQUIRE( !entered.load() );

        ev.signal();
        t.join();
        REQUIRE( entered.load() );
    }

    SECTION( "stress — many signal/reset/wait cycles" )
    {
        const int          iterations = 500;
        std::atomic< int > counter { 0 };
        const unsigned     threads = std::max( 2u, std::thread::hardware_concurrency() );

        thread_guard guard;
        guard.threads.reserve( threads );

        for ( unsigned t = 0; t < threads; ++t ) {
            guard.threads.emplace_back( [ & ] {
                for ( int i = 0; i < iterations; ++i ) {
                    ev.wait();
                    ++counter;
                }
            } );
        }

        const int total = int( threads ) * iterations;
        int       fired = 0;

        while ( fired < total ) {
            ev.signal();
            // Let some threads through, then reset so we can pulse again.
            std::this_thread::yield();
            ev.reset();
            fired = counter.load();
        }

        // Final signal so all threads can drain.
        ev.signal();

        for ( auto& th : guard.threads )
            th.join();
        guard.threads.clear();

        REQUIRE( counter.load() >= total );
    }

    if constexpr ( std::is_same_v< event_t, nova::sync::timed_manual_reset_event > ) {
        SECTION( "wait_for returns true when signalled" )
        {
            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.wait_for( 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( ev.try_wait() );
        }

        SECTION( "wait_for times out when not signalled" )
        {
            bool const ok = ev.wait_for( 20ms );
            REQUIRE( !ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_until returns true before deadline" )
        {
            auto const deadline = std::chrono::steady_clock::now() + 500ms;

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.wait_until( deadline );
            t.join();
            REQUIRE( ok );
        }

        SECTION( "wait_until times out" )
        {
            auto const deadline = std::chrono::steady_clock::now() + 20ms;
            bool const ok       = ev.wait_until( deadline );
            REQUIRE( !ok );
        }

        SECTION( "signal() is lockfree — caller never blocks" )
        {
            const unsigned     n = std::max( 2u, std::thread::hardware_concurrency() );
            std::atomic< int > woken { 0 };

            thread_guard guard;
            guard.threads.reserve( n );
            for ( unsigned i = 0; i < n; ++i )
                guard.threads.emplace_back( [ & ] {
                    ev.wait();
                    ++woken;
                } );

            std::this_thread::sleep_for( 30ms );
            REQUIRE( woken.load() == 0 );

            ev.signal();

            for ( auto& t : guard.threads )
                t.join();
            guard.threads.clear();

            REQUIRE( woken.load() == int( n ) );
            REQUIRE( ev.try_wait() );
        }
    }
}

// =============================================================================
// auto_reset_event variants — correctness tests
// =============================================================================

TEMPLATE_TEST_CASE( "auto_reset_event implementations",
                    "[auto_reset_event]",
                    nova::sync::auto_reset_event,
                    nova::sync::timed_auto_reset_event,
                    nova::sync::native_auto_reset_event )
{
    using event_t = TestType;
    event_t ev;

    SECTION( "initially not set" )
    {
        REQUIRE( !ev.try_wait() );
    }

    SECTION( "initially set via constructor" )
    {
        event_t ev2( true );
        REQUIRE( ev2.try_wait() );
        REQUIRE( !ev2.try_wait() ); // consumed
    }

    SECTION( "post / try_wait consumes signal" )
    {
        ev.signal();
        REQUIRE( ev.try_wait() );  // consumes
        REQUIRE( !ev.try_wait() ); // gone
    }

    SECTION( "signal is idempotent — second signal does not add a second token" )
    {
        ev.signal();
        ev.signal(); // must not accumulate
        REQUIRE( ev.try_wait() );
        REQUIRE( !ev.try_wait() );
    }

    SECTION( "wait on pre-signalled event returns immediately and consumes token" )
    {
        ev.signal();
        ev.wait(); // must not block
        REQUIRE( !ev.try_wait() );
    }

    SECTION( "exactly one waiter woken per signal" )
    {
        const unsigned     n = std::max( 2u, std::thread::hardware_concurrency() );
        std::atomic< int > woken { 0 };

        thread_guard guard;
        guard.threads.reserve( n );

        for ( unsigned i = 0; i < n; ++i ) {
            guard.threads.emplace_back( [ & ] {
                ev.wait();
                ++woken;
            } );
        }

        std::this_thread::sleep_for( 30ms );
        REQUIRE( woken.load() == 0 );

        // Signal n times — each signal should wake exactly one thread.
        for ( unsigned i = 0; i < n; ++i ) {
            ev.signal();
            std::this_thread::sleep_for( 5ms );
            REQUIRE( woken.load() == int( i + 1 ) );
        }

        for ( auto& t : guard.threads )
            t.join();
        guard.threads.clear();
    }

    SECTION( "stress — strict ping-pong producer/consumer" )
    {
        event_t            ev_fwd, ev_ack;
        const int          iterations = 1000;
        std::atomic< int > consumed { 0 };

        std::thread producer( [ & ] {
            for ( int i = 0; i < iterations; ++i ) {
                ev_fwd.signal();
                ev_ack.wait();
            }
        } );

        std::thread consumer( [ & ] {
            for ( int i = 0; i < iterations; ++i ) {
                ev_fwd.wait();
                ++consumed;
                ev_ack.signal();
            }
        } );

        producer.join();
        consumer.join();

        REQUIRE( consumed.load() == iterations );
        REQUIRE( !ev_fwd.try_wait() );
        REQUIRE( !ev_ack.try_wait() );
    }

    if constexpr ( std::is_same_v< event_t, nova::sync::auto_reset_event > ) {
        SECTION( "stress — concurrent try_wait is race-free" )
        {
            const int           pulses = 500;
            const unsigned      takers = std::max( 2u, std::thread::hardware_concurrency() );
            std::atomic< int >  consumed { 0 };
            std::atomic< bool > done { false };

            thread_guard guard;
            guard.threads.reserve( takers );

            for ( unsigned c = 0; c < takers; ++c ) {
                guard.threads.emplace_back( [ & ] {
                    while ( !done.load( std::memory_order_acquire ) ) {
                        if ( ev.try_wait() )
                            ++consumed;
                        else
                            std::this_thread::yield();
                    }
                    if ( ev.try_wait() )
                        ++consumed;
                } );
            }

            for ( int i = 0; i < pulses; ++i ) {
                ev.signal();
                std::this_thread::yield();
            }
            done.store( true, std::memory_order_release );

            for ( auto& t : guard.threads )
                t.join();
            guard.threads.clear();

            REQUIRE( consumed.load() <= pulses );
        }
    }

    if constexpr ( std::is_same_v< event_t, nova::sync::timed_auto_reset_event > ) {
        SECTION( "wait_for returns true when signalled" )
        {
            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.wait_for( 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_for times out when not signalled" )
        {
            bool const ok = ev.wait_for( 20ms );
            REQUIRE( !ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_until returns true before deadline" )
        {
            auto const deadline = std::chrono::steady_clock::now() + 500ms;

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.wait_until( deadline );
            t.join();
            REQUIRE( ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_until times out" )
        {
            auto const deadline = std::chrono::steady_clock::now() + 20ms;
            bool const ok       = ev.wait_until( deadline );
            REQUIRE( !ok );
        }

        SECTION( "signal() is lockfree — caller never blocks" )
        {
            const unsigned     n = std::max( 2u, std::thread::hardware_concurrency() );
            std::atomic< int > woken { 0 };

            thread_guard guard;
            guard.threads.reserve( n );
            for ( unsigned i = 0; i < n; ++i )
                guard.threads.emplace_back( [ & ] {
                    ev.wait();
                    ++woken;
                } );

            std::this_thread::sleep_for( 30ms );

            for ( unsigned i = 0; i < n; ++i )
                ev.signal();

            for ( auto& t : guard.threads )
                t.join();
            guard.threads.clear();

            REQUIRE( woken.load() == int( n ) );
        }
    }
}
