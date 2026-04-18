// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/auto_reset_event.hpp>
#include <nova/sync/manual_reset_event.hpp>
#include <nova/sync/timed_auto_reset_event.hpp>
#include <nova/sync/timed_manual_reset_event.hpp>

#include <thread>

// =============================================================================
// manual_reset_event benchmarks
// =============================================================================
//
// Pattern: one thread signals, another waits in a tight ping-pong loop.
// This measures the round-trip latency (signal + wait) per iteration.

TEMPLATE_TEST_CASE( "manual_reset_event benchmarks",
                    "[!benchmark]",
                    nova::sync::manual_reset_event,
                    nova::sync::timed_manual_reset_event )
{
    using event_t = TestType;

    SECTION( "signal/wait ping-pong (2 threads)" )
    {
        // Two events: ev_a triggers the worker, ev_b triggers the main thread.
        // Main:   signal ev_a → wait ev_b  (per iteration)
        // Worker: wait ev_a → signal ev_b  (per iteration)
        const int ops = 50000;

        BENCHMARK( "signal/wait ping-pong" )
        {
            event_t ev_a, ev_b;

            std::thread worker( [ & ] {
                for ( int i = 0; i < ops; ++i ) {
                    ev_a.wait();
                    ev_a.reset();
                    ev_b.signal();
                }
            } );

            for ( int i = 0; i < ops; ++i ) {
                ev_a.signal();
                ev_b.wait();
                ev_b.reset();
            }

            worker.join();
            return 0;
        };
    }

    SECTION( "try_wait (single-threaded, already set)" )
    {
        const int ops = 1000000;

        BENCHMARK( "try_wait already-set" )
        {
            event_t ev;
            ev.signal();
            int sum = 0;
            for ( int i = 0; i < ops; ++i )
                sum += ev.try_wait() ? 1 : 0;
            return sum;
        };
    }

    SECTION( "signal + reset (single-threaded)" )
    {
        const int ops = 1000000;

        BENCHMARK( "signal + reset" )
        {
            event_t ev;
            for ( int i = 0; i < ops; ++i ) {
                ev.signal();
                ev.reset();
            }
            return 0;
        };
    }
}

// =============================================================================
// auto_reset_event benchmarks
// =============================================================================

TEMPLATE_TEST_CASE( "auto_reset_event benchmarks",
                    "[!benchmark]",
                    nova::sync::auto_reset_event,
                    nova::sync::timed_auto_reset_event )
{
    using event_t = TestType;

    SECTION( "signal/wait ping-pong (2 threads)" )
    {
        // Symmetric ping-pong: signal → wait alternating between two threads.
        const int ops = 50000;

        BENCHMARK( "signal/wait ping-pong" )
        {
            event_t ev_a, ev_b;

            std::thread worker( [ & ] {
                for ( int i = 0; i < ops; ++i ) {
                    ev_a.wait();
                    ev_b.signal();
                }
            } );

            for ( int i = 0; i < ops; ++i ) {
                ev_a.signal();
                ev_b.wait();
            }

            worker.join();
            return 0;
        };
    }

    SECTION( "try_wait (single-threaded, already set)" )
    {
        const int ops = 1000000;

        BENCHMARK( "try_wait already-set" )
        {
            int sum = 0;
            for ( int i = 0; i < ops; ++i ) {
                event_t ev( true );
                sum += ev.try_wait() ? 1 : 0;
            }
            return sum;
        };
    }

    SECTION( "signal + try_wait (single-threaded)" )
    {
        const int ops = 1000000;

        BENCHMARK( "signal + try_wait" )
        {
            event_t ev;
            int     sum = 0;
            for ( int i = 0; i < ops; ++i ) {
                ev.signal();
                sum += ev.try_wait() ? 1 : 0;
            }
            return sum;
        };
    }
}
