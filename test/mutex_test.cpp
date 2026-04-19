// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/concepts.hpp>

#include "mutex_types.hpp"

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
                    std::mutex,
                    std::timed_mutex,
                    std::recursive_mutex,
                    std::recursive_timed_mutex,
                    nova::sync::fast_mutex,
                    nova::sync::fair_mutex,
                    nova::sync::spinlock_mutex,
                    nova::sync::recursive_spinlock_mutex,
                    nova::sync::shared_spinlock_mutex NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES )
{
    using mutex_t = TestType;

    // Compile-time checks using our mutex concepts
    static_assert( nova::sync::concepts::basic_lockable< mutex_t > );
    static_assert( nova::sync::concepts::mutex< mutex_t > );

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
    if constexpr ( nova::sync::concepts::recursive_mutex< mutex_t > ) {
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
    if constexpr ( nova::sync::concepts::shared_mutex< mutex_t > ) {
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

    // Tests specific to timed mutex types
    if constexpr ( nova::sync::concepts::timed_mutex< mutex_t > ) {
        SECTION( "try_lock_for succeeds when uncontended" )
        {
            REQUIRE( m.try_lock_for( 100ms ) );
            m.unlock();
        }

        SECTION( "try_lock_for times out when locked" )
        {
            m.lock();

            bool        acquired = false;
            std::thread other( [ & ] {
                acquired = m.try_lock_for( 30ms );
                if ( acquired )
                    m.unlock();
            } );

            other.join();
            m.unlock();

            REQUIRE( !acquired );
        }

        SECTION( "try_lock_until succeeds when uncontended" )
        {
            REQUIRE( m.try_lock_until( std::chrono::steady_clock::now() + 100ms ) );
            m.unlock();
        }

        SECTION( "try_lock_until times out when locked" )
        {
            m.lock();

            bool        acquired = false;
            std::thread other( [ & ] {
                acquired = m.try_lock_until( std::chrono::steady_clock::now() + 30ms );
                if ( acquired )
                    m.unlock();
            } );

            other.join();
            m.unlock();

            REQUIRE( !acquired );
        }
    }
}

#ifdef NOVA_SYNC_HAS_PTHREAD_RT_MUTEX

#    include <pthread.h>
#    include <sched.h>

namespace {

bool set_thread_sched( int policy, int priority )
{
    struct sched_param param {};
    param.sched_priority = priority;
    return pthread_setschedparam( pthread_self(), policy, &param ) == 0;
}

} // namespace

TEMPLATE_TEST_CASE( "pthread_rt_mutex: prevents priority inversion",
                    "[pthread_rt_mutex][.]",
                    nova::sync::pthread_priority_ceiling_mutex,
                    nova::sync::pthread_priority_inherit_mutex,
                    std::mutex )
{
    using MutexType = TestType;

    const int prio_L    = 10;
    const int prio_M    = 20;
    const int prio_H    = 30;
    const int prio_Main = 40;

    REQUIRE( set_thread_sched( SCHED_FIFO, prio_Main ) );

    auto m = [] {
        if constexpr ( std::is_same_v< MutexType, nova::sync::pthread_priority_ceiling_mutex > ) {
            return MutexType( nova::sync::priority_ceiling( prio_H ) );
        } else {
            return MutexType();
        }
    }();

    std::latch          l_locked { 1 };
    std::latch          h_about_to_block { 1 };
    std::atomic< bool > stop_m { false };
    std::atomic< bool > h_finished { false };

    // L thread (low priority)
    std::thread l_thread( [ & ] {
        REQUIRE( set_thread_sched( SCHED_FIFO, prio_L ) );
        m.lock();
        l_locked.count_down();

        // Wait until H is ready to block
        h_about_to_block.wait();

        // Do some simulated work
        auto start = std::chrono::steady_clock::now();
        while ( std::chrono::steady_clock::now() - start < 50ms ) {
            // Busy wait to ensure it requires CPU time and won't just sleep
        }

        m.unlock();
    } );

    l_locked.wait();

    // H thread (high priority)
    std::thread h_thread( [ & ] {
        REQUIRE( set_thread_sched( SCHED_FIFO, prio_H ) );
        h_about_to_block.count_down();
        m.lock();
        h_finished = true;
        m.unlock();
    } );

    h_about_to_block.wait();

    // Give H a moment to actually enter the kernel and block on the mutex
    std::this_thread::sleep_for( 10ms );

    // M threads (medium priority) - saturate CPUs to cause starvation if L is not boosted
    unsigned                   num_cpus = std::max( 1u, std::thread::hardware_concurrency() );
    std::vector< std::thread > m_threads;
    m_threads.reserve( num_cpus );
    for ( unsigned i = 0; i < num_cpus; ++i ) {
        m_threads.emplace_back( [ & ] {
            REQUIRE( set_thread_sched( SCHED_FIFO, prio_M ) );
            while ( !stop_m ) {
                // busy spin to consume CPU
            }
        } );
    }

    // Wait up to 500ms for H to finish.
    // If priority inheritance/ceiling works, L is boosted to prio_H (30), preempts an M thread (20),
    // finishes its work, and unlocks. H then acquires and finishes.
    // If it fails, M threads (20) starve L (10), so H (30) blocks forever.
    auto start_wait = std::chrono::steady_clock::now();
    while ( !h_finished && std::chrono::steady_clock::now() - start_wait < 500ms ) {
        std::this_thread::sleep_for( 10ms );
    }

    // Capture the result BEFORE stopping the M threads!
    // If we stop the M threads first, the starved L thread will immediately get CPU time
    // and finish, giving us a false positive.
    bool finished_in_time = h_finished.load();

    stop_m = true;

    for ( auto& t : m_threads )
        t.join();
    l_thread.join();
    h_thread.join();

    if constexpr ( std::is_same_v< MutexType, std::mutex > )
        REQUIRE( !finished_in_time ); // without priority inheritance/ceiling, H should fail to acquire within the timeout
    else
        REQUIRE( finished_in_time );

    // Restore normal scheduling
    set_thread_sched( SCHED_OTHER, 0 );
}

#endif // NOVA_SYNC_HAS_PTHREAD_RT_MUTEX
