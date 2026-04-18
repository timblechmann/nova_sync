#include <catch2/catch_all.hpp>

#include <nova/sync/fair_mutex.hpp>
#include <nova/sync/fast_mutex.hpp>
#include <nova/sync/recursive_spinlock_mutex.hpp>
#include <nova/sync/shared_spinlock_mutex.hpp>
#include <nova/sync/spinlock_mutex.hpp>

#include <algorithm>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

using namespace std::chrono_literals;

TEMPLATE_TEST_CASE( "mutex implementations",
                    "[mutex]",
                    nova::sync::fast_mutex,
                    nova::sync::fair_mutex,
                    nova::sync::spinlock_mutex,
                    nova::sync::recursive_spinlock_mutex,
                    nova::sync::shared_spinlock_mutex )
{
    using mutex_t = TestType;
    mutex_t m;

    SECTION( "basic lock/unlock" )
    {
        m.lock();
        m.unlock();
    }

    SECTION( "mutual exclusion across threads" )
    {
        std::atomic< int > counter { 0 };

        const unsigned threads    = std::max( 2u, std::thread::hardware_concurrency() );
        const unsigned iterations = 5000u; // per-thread

        std::vector< std::thread > ths;
        ths.reserve( threads );

        for ( unsigned t = 0; t < threads; ++t ) {
            ths.emplace_back( [ & ] {
                for ( unsigned i = 0; i < iterations; ++i ) {
                    m.lock();
                    ++counter;
                    m.unlock();
                }
            } );
        }

        for ( auto& t : ths )
            t.join();

        REQUIRE( counter.load() == int( threads * iterations ) );
    }

    SECTION( "contended waiter wakes after unlock" )
    {
        std::atomic< int > counter { 0 };

        // Lock the mutex in this thread so other threads will contend
        m.lock();

        const unsigned             threads = std::max( 2u, std::thread::hardware_concurrency() );
        std::vector< std::thread > ths;
        ths.reserve( threads );

        for ( unsigned t = 0; t < threads; ++t ) {
            ths.emplace_back( [ & ] {
                m.lock();
                ++counter; // mark that we entered the critical section
                m.unlock();
            } );
        }

        // Give threads time to reach the contended path and wait
        std::this_thread::sleep_for( 50ms );

        // While we still hold the lock, none of the worker threads should have incremented
        REQUIRE( counter.load() == 0 );

        // Release and let them proceed
        m.unlock();

        for ( auto& t : ths )
            t.join();

        REQUIRE( counter.load() == int( threads ) );
    }

    // Tests specific to the recursive_spinlock_mutex implementation
    if constexpr ( std::is_same_v< mutex_t, nova::sync::recursive_spinlock_mutex > ) {
        SECTION( "recursive try_lock in same thread" )
        {
            // First lock -> owned by this thread
            m.lock();

            // try_lock should succeed for the same owner and increment recursion
            REQUIRE( m.try_lock() );

            // Clean up: two unlocks to fully release
            m.unlock();
            m.unlock();
        }

        SECTION( "other thread cannot acquire while recursion > 1" )
        {
            std::atomic< int > counter { 0 };

            // Acquire twice in this thread
            m.lock();
            m.lock();

            // Worker will repeatedly try_lock until it succeeds
            std::thread worker( [ & ] {
                while ( !m.try_lock() )
                    std::this_thread::yield();

                ++counter; // mark that we entered
                m.unlock();
            } );

            // Give worker time to attempt acquisition; it must not succeed while
            // recursion_count_ > 1 (we still hold the lock twice)
            std::this_thread::sleep_for( 50ms );
            REQUIRE( counter.load() == 0 );

            // Release one level: worker still must not acquire
            m.unlock();
            std::this_thread::sleep_for( 50ms );
            REQUIRE( counter.load() == 0 );

            // Final release: now worker can acquire
            m.unlock();

            worker.join();
            REQUIRE( counter.load() == 1 );
        }
    }

    // Tests specific to the shared_spinlock_mutex implementation
    if constexpr ( std::is_same_v< mutex_t, nova::sync::shared_spinlock_mutex > ) {
        SECTION( "shared locks allow concurrent readers" )
        {
            std::atomic< int > counter { 0 };

            const unsigned threads    = std::max( 2u, std::thread::hardware_concurrency() );
            const unsigned iterations = 2000u; // per-thread

            std::vector< std::thread > ths;
            ths.reserve( threads );

            for ( unsigned t = 0; t < threads; ++t ) {
                ths.emplace_back( [ & ] {
                    for ( unsigned i = 0; i < iterations; ++i ) {
                        m.lock_shared();
                        ++counter;
                        m.unlock_shared();
                    }
                } );
            }

            for ( auto& t : ths )
                t.join();

            REQUIRE( counter.load() == int( threads * iterations ) );
        }

        SECTION( "exclusive lock waits for active readers" )
        {
            std::atomic< int > counter { 0 };

            // Acquire a shared lock in this thread so a writer will contend
            m.lock_shared();

            std::thread writer( [ & ] {
                m.lock();
                ++counter; // mark that writer acquired
                m.unlock();
            } );

            // Give writer time to attempt acquisition; it must be blocked
            std::this_thread::sleep_for( 50ms );
            REQUIRE( counter.load() == 0 );

            // Release the reader and let writer proceed
            m.unlock_shared();
            writer.join();
            REQUIRE( counter.load() == 1 );
        }

        SECTION( "try_lock_shared fails while exclusive locked" )
        {
            m.lock();
            REQUIRE( !m.try_lock_shared() );
            m.unlock();
        }

        SECTION( "try_lock fails while shared locked" )
        {
            m.lock_shared();
            REQUIRE( !m.try_lock() );
            m.unlock_shared();
        }
    }

    // Tests specific to the fair_mutex implementation
    if constexpr ( std::is_same_v< mutex_t, nova::sync::fair_mutex > ) {
        SECTION( "fairness: waiters acquire in FIFO order" )
        {
            const unsigned threads = std::min( 8u, std::max( 2u, std::thread::hardware_concurrency() ) );

            // Hold the lock in this thread so workers will queue
            m.lock();

            std::vector< std::promise< void > > promises( threads );
            std::vector< std::future< void > >  futures;
            futures.reserve( threads );
            for ( unsigned i = 0; i < threads; ++i )
                futures.push_back( promises[ i ].get_future() );

            std::vector< unsigned > acquire_order;
            std::mutex              order_mtx;

            std::vector< std::thread > ths;
            ths.reserve( threads );

            for ( unsigned t = 0; t < threads; ++t ) {
                // move the future into the thread so it can wait on its release
                ths.emplace_back( [ &, fut = std::move( futures[ t ] ), t ]() mutable {
                    fut.wait();

                    m.lock();

                    {
                        std::lock_guard< std::mutex > lk( order_mtx );
                        acquire_order.push_back( t );
                    }

                    m.unlock();
                } );
            }

            // Release threads to attempt acquisition in order
            for ( unsigned t = 0; t < threads; ++t ) {
                promises[ t ].set_value();
                // give the thread a short moment to reach the contended path and get queued
                std::this_thread::sleep_for( 20ms );
            }

            // While we still hold the lock, none should have acquired it
            REQUIRE( acquire_order.empty() );

            // Let them proceed one by one
            m.unlock();

            for ( auto& th : ths )
                th.join();

            REQUIRE( acquire_order.size() == threads );

            // Ensure acquisition order matches the order we released the threads
            for ( unsigned i = 0; i < threads; ++i )
                REQUIRE( acquire_order[ i ] == i );
        }
    }

    SECTION( "stress test: many locks under contention" )
    {
        std::atomic< int > counter { 0 };

        const unsigned threads    = std::max( 2u, std::thread::hardware_concurrency() );
        // keep total work reasonable but exercise contention
        const unsigned total_ops  = 20000u;
        const unsigned iterations = std::max( 1u, total_ops / threads );

        std::vector< std::thread > ths;
        ths.reserve( threads );

        for ( unsigned t = 0; t < threads; ++t ) {
            ths.emplace_back( [ & ] {
                for ( unsigned i = 0; i < iterations; ++i ) {
                    m.lock();
                    ++counter;
                    m.unlock();
                }
            } );
        }

        for ( auto& t : ths )
            t.join();

        REQUIRE( counter.load() == int( threads * iterations ) );
    }
}
