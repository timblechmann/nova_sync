#include <catch2/catch_all.hpp>

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#include <nova/sync/fair_mutex.hpp>
#include <nova/sync/fast_mutex.hpp>
#include <nova/sync/recursive_spinlock_mutex.hpp>
#include <nova/sync/shared_spinlock_mutex.hpp>
#include <nova/sync/spinlock_mutex.hpp>

#ifdef NOVA_SYNC_HAS_QT
#    include <QtCore/QMutex>
#    /*
#     * EXPANSION MACRO
#     * When Qt is available expand to a leading comma + QMutex so it can be
#     * injected into the TEMPLATE_TEST_CASE type-list. When Qt isn't
#     * available expand to nothing.
#     */
#    define NOVA_SYNC_QMUTEX_ARG QMutex,
#else
#    define NOVA_SYNC_QMUTEX_ARG
#endif

static void work()
{
    volatile int x = 0;
    for ( int j = 0; j < 10; ++j )
        x += 1;
}

TEMPLATE_TEST_CASE( "mutex benchmarks",
                    "[!benchmark]",
                    nova::sync::fair_mutex,
                    nova::sync::fast_mutex,
                    nova::sync::spinlock_mutex,
                    nova::sync::recursive_spinlock_mutex,
                    nova::sync::shared_spinlock_mutex,
                    NOVA_SYNC_QMUTEX_ARG std::mutex )
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
        const int      ops_per_thread = 20000; // keep total work reasonable

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
}
