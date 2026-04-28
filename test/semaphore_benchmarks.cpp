// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include "semaphore_types.hpp"

#include <semaphore>
#include <thread>

#ifdef NOVA_SYNC_HAS_QT
#  include <QtCore/QSemaphore>
#  define NOVA_SYNC_QT_SEMAPHORE_TYPE , QSemaphore
#else
#  define NOVA_SYNC_QT_SEMAPHORE_TYPE
#endif


// =============================================================================
// Semaphore benchmarks
// =============================================================================

TEMPLATE_TEST_CASE( "semaphore benchmarks",
                    "[!benchmark]",
                    std::counting_semaphore<>,
                    NOVA_SYNC_ALL_SEMAPHORE_TYPES NOVA_SYNC_QT_SEMAPHORE_TYPE )
{
    using sem_t = TestType;

    SECTION( "release/acquire ping-pong (2 threads)" )
    {
        const int ops = 50000;

        BENCHMARK( "release/acquire ping-pong" )
        {
            sem_t sem_a( 0 ), sem_b( 0 );

            std::thread worker( [ & ] {
                for ( int i = 0; i < ops; ++i ) {
                    sem_a.acquire();
                    sem_b.release();
                }
            } );

            for ( int i = 0; i < ops; ++i ) {
                sem_a.release();
                sem_b.acquire();
            }

            worker.join();
            return 0;
        };
    }

    SECTION( "try_acquire (single-threaded, already released)" )
    {
        const int ops = 1000000;

        BENCHMARK( "try_acquire already-released" )
        {
            sem_t sem( ops );
            int   sum = 0;
            for ( int i = 0; i < ops; ++i )
                sum += sem.try_acquire() ? 1 : 0;
            return sum;
        };
    }

    SECTION( "release + try_acquire (single-threaded)" )
    {
        const int ops = 1000000;

        BENCHMARK( "release + try_acquire" )
        {
            sem_t sem( 0 );
            int   sum = 0;
            for ( int i = 0; i < ops; ++i ) {
                sem.release();
                sum += sem.try_acquire() ? 1 : 0;
            }
            return sum;
        };
    }
}
