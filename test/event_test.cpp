// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/event/concepts.hpp>
#include <nova/sync/event/native_auto_reset_event.hpp>
#include <nova/sync/event/native_manual_reset_event.hpp>
#include <nova/sync/event/parking_auto_reset_event.hpp>
#include <nova/sync/event/parking_manual_reset_event.hpp>
#include <nova/sync/event/timed_auto_reset_event.hpp>
#include <nova/sync/event/timed_manual_reset_event.hpp>

// Validate concepts for the concrete types we exercise in this test file.
static_assert( nova::sync::concepts::manual_reset_event< nova::sync::manual_reset_event > );
static_assert( nova::sync::concepts::timed_event< nova::sync::timed_manual_reset_event > );
static_assert( nova::sync::concepts::native_async_event< nova::sync::native_manual_reset_event > );
static_assert( nova::sync::concepts::auto_reset_event< nova::sync::auto_reset_event > );
static_assert( nova::sync::concepts::timed_event< nova::sync::timed_auto_reset_event > );
#if defined( _WIN32 )
static_assert( nova::sync::concepts::native_async_event< nova::sync::native_auto_reset_event > );
#else
static_assert( !nova::sync::concepts::native_async_event< nova::sync::native_auto_reset_event > );
#endif

#include <algorithm>
#include <atomic>
#include <latch>
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

    if constexpr ( nova::sync::concepts::timed_event< event_t > ) {
        SECTION( "wait_for returns true when signalled" )
        {
            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.try_wait_for( 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( ev.try_wait() );
        }

        SECTION( "wait_for times out when not signalled" )
        {
            bool const ok = ev.try_wait_for( 20ms );
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

            bool const ok = ev.try_wait_until( deadline );
            t.join();
            REQUIRE( ok );
        }

        SECTION( "wait_until times out" )
        {
            auto const deadline = std::chrono::steady_clock::now() + 20ms;
            bool const ok       = ev.try_wait_until( deadline );
            REQUIRE( !ok );
        }

        SECTION( "signal() is lockfree - caller never blocks" )
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

    if constexpr ( nova::sync::concepts::native_async_event< event_t > && nova::sync::concepts::timed_event< event_t > ) {
        SECTION( "wait_for returns true when signalled (native)" )
        {
            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.try_wait_for( 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( ev.try_wait() );
        }

        SECTION( "wait_for times out when not signalled (native)" )
        {
            bool const ok = ev.try_wait_for( 20ms );
            REQUIRE( !ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_until (steady_clock) wakes up when signalled (native)" )
        {
            using clock = std::chrono::steady_clock;

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.try_wait_until( clock::now() + 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( ev.try_wait() );
        }

        SECTION( "wait_until (system_clock) wakes up when signalled (native)" )
        {
            using system = std::chrono::system_clock;

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.try_wait_until( system::now() + 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( ev.try_wait() );
        }

        SECTION( "wait_for wakes up when signalled (native - long)" )
        {
            using clock = std::chrono::steady_clock;

            // Pre-signal then reset so that waiter will block
            ev.reset();

            std::latch started { 1 };
            bool       acquired_in_a = false;

            std::thread a( [ & ] {
                started.count_down();
                auto t0       = clock::now();
                acquired_in_a = ev.try_wait_for( 10s );
                auto elapsed  = clock::now() - t0;
                INFO( "elapsed = " << elapsed );
                // Expect wakeup after ~5s, with 2-8s margin for CI scheduling delays.
                // Lower bound (2s) accounts for clock skew and measurement noise.
                // Upper bound (8s) allows for significant scheduling latency on virtualized CI.
                REQUIRE( elapsed >= 2s );
                REQUIRE( elapsed < 8s );
            } );

            started.wait();
            std::this_thread::sleep_for( 200ms );

            // Thread B: hold for ~5s then signal
            std::this_thread::sleep_for( 5s );
            ev.signal();

            a.join();
            REQUIRE( acquired_in_a );
        }

        SECTION( "wait_until (steady_clock) wakes up when signalled (native - long)" )
        {
            using clock = std::chrono::steady_clock;

            ev.reset();

            std::latch started { 1 };
            bool       acquired_in_a = false;

            std::thread a( [ & ] {
                started.count_down();
                auto t0       = clock::now();
                acquired_in_a = ev.try_wait_until( t0 + 10s );
                auto elapsed  = clock::now() - t0;
                INFO( "elapsed = " << elapsed );
                // Expect wakeup after ~5s, with 2-8s margin for CI scheduling delays.
                REQUIRE( elapsed >= 2s );
                REQUIRE( elapsed < 8s );
            } );

            started.wait();
            std::this_thread::sleep_for( 200ms );

            std::this_thread::sleep_for( 5s );
            ev.signal();

            a.join();
            REQUIRE( acquired_in_a );
        }

        SECTION( "wait_until (system_clock) wakes up when signalled (native - long)" )
        {
            using steady = std::chrono::steady_clock;
            using system = std::chrono::system_clock;

            ev.reset();

            std::latch started { 1 };
            bool       acquired_in_a = false;

            std::thread a( [ & ] {
                started.count_down();
                auto t0       = steady::now();
                acquired_in_a = ev.try_wait_until( system::now() + 10s );
                auto elapsed  = steady::now() - t0;
                INFO( "elapsed = " << elapsed );
                // Expect wakeup after ~5s, with 2-8s margin for CI scheduling delays.
                REQUIRE( elapsed >= 2s );
                REQUIRE( elapsed < 8s );
            } );

            started.wait();
            std::this_thread::sleep_for( 200ms );

            std::this_thread::sleep_for( 5s );
            ev.signal();

            a.join();
            REQUIRE( acquired_in_a );
        }
    }
}


// =============================================================================
// manual_reset_event variants — stress tests
// =============================================================================

TEMPLATE_TEST_CASE( "manual_reset_event implementations (stress tests)",
                    "[manual_reset_event][stress]",
                    nova::sync::manual_reset_event,
                    nova::sync::timed_manual_reset_event,
                    nova::sync::native_manual_reset_event )
{
    using event_t = TestType;
    event_t ev;

    SECTION( "stress - many signal/reset/wait cycles" )
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

    SECTION( "signal is idempotent - second signal does not add a second token" )
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
            // Spin-wait for the woken thread to increment the counter.
            // A fixed sleep is insufficient on loaded CI runners.
            auto deadline = std::chrono::steady_clock::now() + 2s;
            while ( woken.load( std::memory_order_acquire ) != int( i + 1 ) ) {
                if ( std::chrono::steady_clock::now() > deadline )
                    break;
                std::this_thread::sleep_for( 1ms );
            }
            REQUIRE( woken.load() == int( i + 1 ) );
        }

        for ( auto& t : guard.threads )
            t.join();
        guard.threads.clear();
    }

    if constexpr ( nova::sync::concepts::timed_event< event_t > ) {
        SECTION( "wait_for returns true when signalled" )
        {
            std::thread t( [ & ] {
                std::this_thread::sleep_for( 10ms );
                ev.signal();
            } );

            bool const ok = ev.try_wait_for( 500ms );
            t.join();
            REQUIRE( ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_for times out when not signalled" )
        {
            bool const ok = ev.try_wait_for( 20ms );
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

            bool const ok = ev.try_wait_until( deadline );
            t.join();
            REQUIRE( ok );
            REQUIRE( !ev.try_wait() );
        }

        SECTION( "wait_until times out" )
        {
            auto const deadline = std::chrono::steady_clock::now() + 20ms;
            bool const ok       = ev.try_wait_until( deadline );
            REQUIRE( !ok );
        }

        SECTION( "signal() is lockfree - caller never blocks" )
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


// =============================================================================
// auto_reset_event variants — stress tests
// =============================================================================

TEMPLATE_TEST_CASE( "auto_reset_event implementations (stress tests)",
                    "[auto_reset_event][stress]",
                    nova::sync::auto_reset_event,
                    nova::sync::timed_auto_reset_event,
                    nova::sync::native_auto_reset_event )
{
    using event_t = TestType;
    event_t ev;

    SECTION( "stress - strict ping-pong producer/consumer" )
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

    if constexpr ( nova::sync::concepts::auto_reset_event< event_t > && !nova::sync::concepts::timed_event< event_t > ) {
        SECTION( "stress - concurrent try_wait is race-free" )
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
}

#if __has_include( <unistd.h> ) && __has_include( <poll.h> )

#  include <fcntl.h>
#  include <nova/sync/detail/syscall.hpp>
#  include <poll.h>
#  include <unistd.h>

#  include "event_types.hpp"

TEMPLATE_TEST_CASE( "event: poll_intr zero-timeout is non-blocking", "[event]", NOVA_SYNC_ASYNC_MANUAL_EVENT_TYPES )
{
    using event_t = TestType;

    // This test verifies that poll_intr with 0ms timeout still calls poll()
    // and doesn't short-circuit. Previously, the rewrite would return 0
    // immediately without calling poll(), breaking non-blocking readiness checks.

    // Create a pipe
    int pipefd[ 2 ];
    REQUIRE( ::pipe( pipefd ) == 0 );

    struct pollfd pfd { pipefd[ 0 ], POLLIN, 0 };

    // Empty pipe: should return 0 (not readable) with 0ms timeout
    int rc = nova::sync::detail::poll_intr( pfd, std::chrono::milliseconds( 0 ) );
    REQUIRE( rc == 0 );

    // Write data
    uint8_t byte = 1;
    REQUIRE( ::write( pipefd[ 1 ], &byte, 1 ) == 1 );

    // Now pipe should be readable with 0ms timeout
    rc = nova::sync::detail::poll_intr( pfd, std::chrono::milliseconds( 0 ) );
    REQUIRE( rc > 0 );

    // Cleanup
    ::close( pipefd[ 0 ] );
    ::close( pipefd[ 1 ] );
}

#endif // __has_include( <unistd.h> ) && __has_include( <poll.h> )
