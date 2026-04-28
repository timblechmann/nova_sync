// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/semaphore/concepts.hpp>
#include <nova/sync/semaphore/fast_semaphore.hpp>

#include "semaphore_types.hpp"

#include <algorithm>
#include <atomic>
#include <latch>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// Validate concepts for portable types.
static_assert( nova::sync::concepts::counting_semaphore< nova::sync::fast_semaphore > );

// =============================================================================
// Shared helpers
// =============================================================================

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
// counting_semaphore — basic correctness tests
// =============================================================================

TEMPLATE_TEST_CASE( "counting_semaphore implementations", "[semaphore]", NOVA_SYNC_ALL_SEMAPHORE_TYPES )
{
    using sem_t = TestType;

    SECTION( "initial count 0 — try_acquire fails" )
    {
        sem_t sem( 0 );
        REQUIRE( !sem.try_acquire() );
    }

    SECTION( "initial count 1 — try_acquire succeeds once" )
    {
        sem_t sem( 1 );
        REQUIRE( sem.try_acquire() );
        REQUIRE( !sem.try_acquire() );
    }

    SECTION( "initial count N — try_acquire succeeds N times" )
    {
        const int N = 5;
        sem_t     sem( N );
        for ( int i = 0; i < N; ++i )
            REQUIRE( sem.try_acquire() );
        REQUIRE( !sem.try_acquire() );
    }

    SECTION( "release then acquire" )
    {
        sem_t sem( 0 );
        sem.release();
        REQUIRE( sem.try_acquire() );
        REQUIRE( !sem.try_acquire() );
    }

    SECTION( "release(n) adds n tokens" )
    {
        sem_t sem( 0 );
        sem.release( 3 );
        REQUIRE( sem.try_acquire() );
        REQUIRE( sem.try_acquire() );
        REQUIRE( sem.try_acquire() );
        REQUIRE( !sem.try_acquire() );
    }

    SECTION( "acquire blocks then wakes" )
    {
        sem_t               sem( 0 );
        std::atomic< bool > acquired { false };

        std::thread t( [ & ] {
            sem.acquire();
            acquired.store( true );
        } );

        std::this_thread::sleep_for( 30ms );
        REQUIRE( !acquired.load() );

        sem.release();
        t.join();
        REQUIRE( acquired.load() );
    }

    SECTION( "acquire on pre-released semaphore returns immediately" )
    {
        sem_t sem( 1 );
        sem.acquire(); // must not block
        REQUIRE( !sem.try_acquire() );
    }

    SECTION( "producer-consumer ping-pong" )
    {
        sem_t              sem_fwd( 0 ), sem_ack( 0 );
        const int          iterations = 500;
        std::atomic< int > consumed { 0 };

        std::thread producer( [ & ] {
            for ( int i = 0; i < iterations; ++i ) {
                sem_fwd.release();
                sem_ack.acquire();
            }
        } );

        std::thread consumer( [ & ] {
            for ( int i = 0; i < iterations; ++i ) {
                sem_fwd.acquire();
                ++consumed;
                sem_ack.release();
            }
        } );

        producer.join();
        consumer.join();

        REQUIRE( consumed.load() == iterations );
    }

    SECTION( "multiple waiters — all wake after release(n)" )
    {
        const unsigned     n = std::max( 2u, std::thread::hardware_concurrency() );
        sem_t              sem( 0 );
        std::atomic< int > woken { 0 };

        thread_guard guard;
        guard.threads.reserve( n );

        for ( unsigned i = 0; i < n; ++i ) {
            guard.threads.emplace_back( [ & ] {
                sem.acquire();
                ++woken;
            } );
        }

        std::this_thread::sleep_for( 30ms );
        REQUIRE( woken.load() == 0 );

        sem.release( std::ptrdiff_t( n ) );

        for ( auto& t : guard.threads )
            t.join();
        guard.threads.clear();

        REQUIRE( woken.load() == int( n ) );
    }

    SECTION( "stress — concurrent try_acquire is race-free" )
    {
        const int           pulses = 500;
        const unsigned      takers = std::max( 2u, std::thread::hardware_concurrency() );
        sem_t               sem( 0 );
        std::atomic< int >  consumed { 0 };
        std::atomic< bool > done { false };

        thread_guard guard;
        guard.threads.reserve( takers );

        for ( unsigned c = 0; c < takers; ++c ) {
            guard.threads.emplace_back( [ & ] {
                while ( !done.load( std::memory_order_acquire ) ) {
                    if ( sem.try_acquire() )
                        ++consumed;
                    else
                        std::this_thread::yield();
                }
                // drain remaining
                while ( sem.try_acquire() )
                    ++consumed;
            } );
        }

        for ( int i = 0; i < pulses; ++i ) {
            sem.release();
            std::this_thread::yield();
        }
        done.store( true, std::memory_order_release );

        for ( auto& t : guard.threads )
            t.join();
        guard.threads.clear();

        REQUIRE( consumed.load() == pulses );
    }
}

// =============================================================================
// Stress tests
// =============================================================================

TEMPLATE_TEST_CASE( "counting_semaphore stress", "[stress]", NOVA_SYNC_ALL_SEMAPHORE_TYPES )
{
    using sem_t = TestType;

    SECTION( "stress — many signal/release/acquire cycles" )
    {
        const int          iterations = 500;
        std::atomic< int > counter { 0 };
        const unsigned     threads = std::max( 2u, std::thread::hardware_concurrency() );

        thread_guard guard;
        guard.threads.reserve( threads );

        for ( unsigned t = 0; t < threads; ++t ) {
            guard.threads.emplace_back( [ & ] {
                for ( int i = 0; i < iterations; ++i ) {
                    sem_t sem( 0 );
                    sem.release();
                    ++counter;
                }
            } );
        }

        for ( auto& th : guard.threads )
            th.join();
        guard.threads.clear();

        REQUIRE( counter.load() == int( threads ) * iterations );
    }

    // -------------------------------------------------------------------------
    // Timed variants
    // -------------------------------------------------------------------------

    if constexpr ( nova::sync::concepts::timed_counting_semaphore< sem_t > ) {
        SECTION( "try_acquire_for returns true when released" )
        {
            sem_t sem( 0 );

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 50ms );
                sem.release();
            } );

            bool const ok = sem.try_acquire_for( 1s );
            t.join();
            REQUIRE( ok );
        }

        SECTION( "try_acquire_for times out" )
        {
            sem_t      sem( 0 );
            bool const ok = sem.try_acquire_for( 50ms );
            REQUIRE( !ok );
        }

        SECTION( "try_acquire_for waits approximately the specified duration" )
        {
            sem_t sem( 0 );
            auto  t0      = std::chrono::steady_clock::now();
            bool  ok      = sem.try_acquire_for( 1s );
            auto  elapsed = std::chrono::steady_clock::now() - t0;

            REQUIRE( !ok );
            // Expect ~1s, with 0.7–1.3s margin for CI scheduling delays
            REQUIRE( elapsed >= 700ms );
            REQUIRE( elapsed < 2s );
        }

        SECTION( "try_acquire_until returns true before deadline (steady_clock)" )
        {
            sem_t      sem( 0 );
            auto const deadline = std::chrono::steady_clock::now() + 1s;

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 50ms );
                sem.release();
            } );

            bool const ok = sem.try_acquire_until( deadline );
            t.join();
            REQUIRE( ok );
        }

        SECTION( "try_acquire_until times out (steady_clock)" )
        {
            sem_t      sem( 0 );
            auto const deadline = std::chrono::steady_clock::now() + 50ms;
            bool const ok       = sem.try_acquire_until( deadline );
            REQUIRE( !ok );
        }

        SECTION( "try_acquire_until waits approximately until deadline (steady_clock)" )
        {
            sem_t      sem( 0 );
            auto const t0       = std::chrono::steady_clock::now();
            auto const deadline = t0 + 1s;
            bool       ok       = sem.try_acquire_until( deadline );
            auto       elapsed  = std::chrono::steady_clock::now() - t0;

            REQUIRE( !ok );
            // Expect ~1s, with 0.7–1.3s margin for CI scheduling delays
            REQUIRE( elapsed >= 700ms );
            REQUIRE( elapsed < 2s );
        }

        SECTION( "try_acquire_until returns true before deadline (system_clock)" )
        {
            using system = std::chrono::system_clock;
            sem_t      sem( 0 );
            auto const deadline = system::now() + 1s;

            std::thread t( [ & ] {
                std::this_thread::sleep_for( 50ms );
                sem.release();
            } );

            bool const ok = sem.try_acquire_until( deadline );
            t.join();
            REQUIRE( ok );
        }

        SECTION( "try_acquire_until times out (system_clock)" )
        {
            using system = std::chrono::system_clock;
            sem_t      sem( 0 );
            auto const deadline = system::now() + 50ms;
            bool const ok       = sem.try_acquire_until( deadline );
            REQUIRE( !ok );
        }

        SECTION( "try_acquire_until waits approximately until deadline (system_clock)" )
        {
            using system = std::chrono::system_clock;
            using steady = std::chrono::steady_clock;

            sem_t      sem( 0 );
            auto const t0       = steady::now();
            auto const deadline = system::now() + 1s;
            bool       ok       = sem.try_acquire_until( deadline );
            auto       elapsed  = steady::now() - t0;

            REQUIRE( !ok );
            // Expect ~1s, with 0.7–1.3s margin for CI scheduling delays
            REQUIRE( elapsed >= 700ms );
            REQUIRE( elapsed < 2s );
        }
    }
}
