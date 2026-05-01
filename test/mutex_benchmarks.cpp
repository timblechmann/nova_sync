// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#include <nova/sync/mutex/concepts.hpp>

#ifdef NOVA_SYNC_HAS_QT
#  include <QtCore/qmutex.h>
#  define NOVA_SYNC_QT_MUTEX_TYPE , QMutex
#else
#  define NOVA_SYNC_QT_MUTEX_TYPE
#endif

#include "mutex_types.hpp"

static void work()
{
    volatile int x = 0;
    for ( int j = 0; j < 10; ++j )
        x = x + 1;
}

TEMPLATE_TEST_CASE( "mutex benchmarks",
                    "[!benchmark]",
                    std::mutex,
                    std::timed_mutex,
                    std::recursive_mutex,
                    std::recursive_timed_mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES NOVA_SYNC_QT_MUTEX_TYPE )
{
    using mutex_t = TestType;

    SECTION( "single-threaded" )
    {
        const int ops = 1000000;

        BENCHMARK( "single-threaded" )
        {
            mutex_t m;
            for ( int i = 0; i < ops; ++i ) {
                m.lock();
                work();
                m.unlock();
            }
            return 0;
        };
    }

    SECTION( "multi-threaded" )
    {
        const unsigned threads        = std::max( 2u, std::thread::hardware_concurrency() );
        const int      ops_per_thread = 10000; // keep total work reasonable

        BENCHMARK( "multi-threaded" )
        {
            mutex_t                    m;
            std::vector< std::thread > ths;
            ths.reserve( threads );
            for ( unsigned t = 0; t < threads; ++t ) {
                ths.emplace_back( [ & ] {
                    for ( int i = 0; i < ops_per_thread; ++i ) {
                        m.lock();
                        work();
                        m.unlock();
                    }
                } );
            }
            for ( auto& th : ths )
                th.join();
            return 0;
        };
    }

    SECTION( "multi-threaded (high contention)" )
    {
        const unsigned threads        = std::max( 2u, std::thread::hardware_concurrency() * 2 );
        const int      ops_per_thread = 10000; // keep total work reasonable

        BENCHMARK( "multi-threaded (high contention)" )
        {
            mutex_t                    m;
            std::vector< std::thread > ths;
            ths.reserve( threads );
            for ( unsigned t = 0; t < threads; ++t ) {
                ths.emplace_back( [ & ] {
                    for ( int i = 0; i < ops_per_thread; ++i ) {
                        m.lock();
                        work();
                        m.unlock();
                    }
                } );
            }
            for ( auto& th : ths )
                th.join();
            return 0;
        };
    }
}
