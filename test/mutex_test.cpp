// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/mutex/support/async_waiter_guard.hpp>

#include "mutex_types.hpp"

#include <algorithm>
#include <atomic>
#include <future>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Basic mutex tests — all types
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "mutex: basic lock/unlock",
                    "[mutex]",
                    std::mutex,
                    std::timed_mutex,
                    std::recursive_mutex,
                    std::recursive_timed_mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
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
}


// ---------------------------------------------------------------------------
// Basic mutex tests — stress tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "mutex: basic lock/unlock (stress tests)",
                    "[mutex][stress]",
                    std::mutex,
                    std::timed_mutex,
                    std::recursive_mutex,
                    std::recursive_timed_mutex,
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    using mutex_t = TestType;

    mutex_t m;

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

// ---------------------------------------------------------------------------
// try_lock tests — all annotated types, branched by recursive vs non-recursive
// ---------------------------------------------------------------------------
//
// Rule (Clang Thread Safety Analysis §"No conditionally held locks"):
//   Every path through a SECTION must either hold the lock on exit or not —
//   never "maybe".  We use the `bool acquired; if (!acquired) return;` pattern
//   to give the analyzer a single unconditional path after try_lock.
//
// Recursive types carry NOVA_SYNC_REENTRANT_CAPABILITY so the analyzer accepts
// re-locking without a "mutex already held" diagnostic.

TEMPLATE_TEST_CASE( "mutex: try_lock", "[mutex]", NOVA_SYNC_ALL_MUTEX_TYPES )
{
    using mutex_t = TestType;
    mutex_t m;

    SECTION( "try_lock succeeds when uncontended" )
    {
        bool acquired = m.try_lock();
        REQUIRE( acquired );
        if ( !acquired )
            return;
        m.unlock();
    }

    if constexpr ( !nova::sync::concepts::recursive_mutex< mutex_t > ) {
        SECTION( "try_lock fails when already held by this thread (non-recursive)" )
        {
            m.lock();
            REQUIRE( !m.try_lock() );
            m.unlock();
        }
    }

    if constexpr ( nova::sync::concepts::recursive_mutex< mutex_t > ) {
        SECTION( "try_lock succeeds when already held by this thread (re-entry)" )
        {
            // The analyzer cannot model recursive re-entry: TRY_ACQUIRE on an
            // already-held mutex always warns even on reentrant types.
            [ & ]() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS {
                m.lock();
                bool acquired = m.try_lock();
                REQUIRE( acquired );
                m.unlock(); // release re-entry
                m.unlock(); // release initial
            }();
        }

        SECTION( "other thread cannot acquire while recursion > 1" )
        {
            std::atomic< int > counter { 0 };

            // Same limitation: double-lock requires suppression.
            [ & ]() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS {
                m.lock();
                m.lock();

                // Worker spins on try_lock: exits the while only when lock is acquired.
                std::thread worker( [ & ] {
                    while ( !m.try_lock() )
                        std::this_thread::yield();
                    ++counter;
                    m.unlock();
                } );

                // Must not acquire while we hold the lock twice.
                std::this_thread::sleep_for( 50ms );
                REQUIRE( counter.load() == 0 );

                // Release one level — worker still must not acquire.
                m.unlock();
                std::this_thread::sleep_for( 50ms );
                REQUIRE( counter.load() == 0 );

                // Final release — now worker can acquire.
                m.unlock();

                worker.join();
                REQUIRE( counter.load() == 1 );
            }();
        }
    }
}


// ---------------------------------------------------------------------------
// Shared mutex tests
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "mutex: shared locking", "[mutex]", NOVA_SYNC_SHARED_MUTEX_TYPES )
{
    using mutex_t = TestType;

    mutex_t m;

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

// ---------------------------------------------------------------------------
// Fair mutex — FIFO ordering test
// ---------------------------------------------------------------------------

TEST_CASE( "mutex: fair_mutex FIFO ordering", "[mutex]" )
{
    nova::sync::fair_mutex m;

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

// ---------------------------------------------------------------------------
// Timed mutex tests — only for types that have try_lock_for / try_lock_until
// ---------------------------------------------------------------------------

#ifdef NOVA_SYNC_TIMED_MUTEX_TYPES

static_assert( nova::sync::concepts::timed_mutex< nova::sync::fast_mutex > );
static_assert( nova::sync::concepts::timed_mutex< nova::sync::fair_mutex > );

TEMPLATE_TEST_CASE( "mutex: timed locking", "[mutex]", NOVA_SYNC_TIMED_MUTEX_TYPES )
{
    using mutex_t = TestType;

    mutex_t m;

    SECTION( "try_lock_for succeeds when uncontended" )
    {
        bool acquired = m.try_lock_for( 100ms );
        REQUIRE( acquired );
        if ( !acquired )
            return;
        m.unlock();
    }

    SECTION( "try_lock_for times out when locked" )
    {
        m.lock();

        bool        acquired = false;
        std::thread other( [ & ] {
            if ( m.try_lock_for( 30ms ) ) {
                acquired = true;
                m.unlock();
            }
        } );

        other.join();
        m.unlock();

        REQUIRE( !acquired );
    }

    SECTION( "try_lock_until succeeds when uncontended" )
    {
        bool acquired = m.try_lock_until( std::chrono::steady_clock::now() + 100ms );
        REQUIRE( acquired );
        if ( !acquired )
            return;
        m.unlock();
    }

    SECTION( "try_lock_until times out when locked" )
    {
        m.lock();

        bool        acquired = false;
        std::thread other( [ & ] {
            if ( m.try_lock_until( std::chrono::steady_clock::now() + 30ms ) ) {
                acquired = true;
                m.unlock();
            }
        } );

        other.join();
        m.unlock();

        REQUIRE( !acquired );
    }

    // -----------------------------------------------------------------------
    // Wakeup tests: verify that try_lock_for/try_lock_until wakes up correctly
    // when the lock is released by another thread, not just by timeout.
    //
    // Thread A waits up to 10 s.  Thread B holds the lock for 5 s then
    // releases it.  We expect:
    //   • try_lock returns true (woken by B, not timed out)
    //   • elapsed time is in [3 s, 9 s]  — well below the 10 s budget but
    //     well above zero, with generous margins for slow/loaded cloud VMs.
    // -----------------------------------------------------------------------
    SECTION( "try_lock_for wakes up when lock is released (not timeout)" )
    {
        using clock = std::chrono::steady_clock;

        // Pre-acquire so thread A will block immediately.
        m.lock();

        std::latch started { 1 }; // signals when thread A is about to wait
        bool       acquired_in_a = false;

        std::thread a( [ & ] {
            started.count_down();
            auto t0 = clock::now();
            // m.try_lock_for has NOVA_SYNC_TRY_ACQUIRE annotation so the analyzer
            // knows the lock is conditionally acquired on true return.
            if ( m.try_lock_for( 10s ) ) {
                acquired_in_a = true;
                auto elapsed  = clock::now() - t0;
                INFO( "elapsed = " << elapsed );
                REQUIRE( elapsed >= 3s );
                REQUIRE( elapsed < 9s );
                m.unlock();
            }
        } );

        // Give thread A time to enter the wait.
        started.wait();
        std::this_thread::sleep_for( 200ms ); // small extra margin

        // Thread B: hold 5 s, then release.
        std::this_thread::sleep_for( 5s );
        m.unlock();

        a.join();
        REQUIRE( acquired_in_a );
    }

    SECTION( "try_lock_until (steady_clock) wakes up when lock is released (not timeout)" )
    {
        using clock = std::chrono::steady_clock;

        m.lock();

        std::latch started { 1 };
        bool       acquired_in_a = false;

        std::thread a( [ & ] {
            started.count_down();
            auto t0 = clock::now();
            if ( m.try_lock_until( t0 + 10s ) ) {
                acquired_in_a = true;
                auto elapsed  = clock::now() - t0;
                INFO( "elapsed = " << elapsed );
                REQUIRE( elapsed >= 3s );
                REQUIRE( elapsed < 9s );
                m.unlock();
            }
        } );

        started.wait();

        std::this_thread::sleep_for( 5s );
        m.unlock();

        a.join();
        REQUIRE( acquired_in_a );
    }

    SECTION( "try_lock_until (system_clock) wakes up when lock is released (not timeout)" )
    {
        using steady = std::chrono::steady_clock;
        using system = std::chrono::system_clock;

        m.lock();

        std::latch started { 1 };
        bool       acquired_in_a = false;

        std::thread a( [ & ] {
            started.count_down();
            auto t0 = steady::now();
            if ( m.try_lock_until( system::now() + 10s ) ) {
                acquired_in_a = true;
                auto elapsed  = steady::now() - t0;
                INFO( "elapsed = " << elapsed );
                REQUIRE( elapsed >= 3s );
                REQUIRE( elapsed < 9s );
                m.unlock();
            }
        } );

        started.wait();
        std::this_thread::sleep_for( 5s );
        m.unlock();

        a.join();
        REQUIRE( acquired_in_a );
    }
}

#endif // NOVA_SYNC_TIMED_MUTEX_TYPES

#ifdef NOVA_SYNC_HAS_PTHREAD_RT_MUTEX

#  include <pthread.h>
#  include <sched.h>

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

// ---------------------------------------------------------------------------
// async_waiter_guard tests
// ---------------------------------------------------------------------------

#ifdef NOVA_SYNC_ASYNC_MUTEX_TYPES

TEMPLATE_TEST_CASE( "async_waiter_guard: try_acquire succeeds when lock is free",
                    "[async_waiter_guard]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // try_acquire() transfers lock ownership through a reference member (mtx_).
    // The analyzer cannot match guard.mtx_ to the caller's mtx variable, so
    // suppression is required here — this is a genuine analyzer limitation.
    [ & ]() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS {
        TestType mtx;

        nova::sync::detail::async_waiter_guard< TestType > guard( mtx );
        REQUIRE( guard.active() );

        bool acquired = guard.try_acquire();
        REQUIRE( acquired );
        REQUIRE_FALSE( guard.active() ); // released internally by try_acquire
        mtx.unlock();
    }();
}

TEMPLATE_TEST_CASE( "async_waiter_guard: try_acquire returns false when locked",
                    "[async_waiter_guard]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // Same limitation as above.
    [ & ]() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS {
        TestType mtx;
        mtx.lock();

        {
            nova::sync::detail::async_waiter_guard< TestType > guard( mtx );
            REQUIRE( guard.active() );

            bool acquired = guard.try_acquire();
            REQUIRE_FALSE( acquired );
            REQUIRE( guard.active() );
        }

        mtx.unlock();

        bool ok = mtx.try_lock();
        REQUIRE( ok );
        if ( !ok )
            return;
        mtx.unlock();
    }();
}

TEMPLATE_TEST_CASE( "async_waiter_guard: release is idempotent", "[async_waiter_guard]", NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    TestType mtx;

    nova::sync::detail::async_waiter_guard< TestType > guard( mtx );
    REQUIRE( guard.active() );

    guard.release();
    REQUIRE_FALSE( guard.active() );

    guard.release(); // second call must be a no-op
    REQUIRE_FALSE( guard.active() );
}

TEMPLATE_TEST_CASE( "async_waiter_guard: adopt constructor does not double-register",
                    "[async_waiter_guard]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // Simulate the lock_slow() pattern: add_async_waiter() called manually,
    // then guard adopted.  Lock must be acquirable and waiter count must be
    // balanced (exactly one remove_async_waiter on exit).
    TestType mtx;

    if constexpr ( nova::sync::concepts::async_waiter_mutex< TestType > ) {
        // Manually register once
        mtx.add_async_waiter();

        // Adopt — must NOT call add_async_waiter() again
        nova::sync::detail::async_waiter_guard< TestType > guard( mtx, nova::sync::detail::adopt_async_waiter );
        REQUIRE( guard.active() );

        guard.release(); // calls remove_async_waiter once — should be balanced
        REQUIRE_FALSE( guard.active() );

        // If waiter count is balanced, try_lock should now work and unlock()
        // should not post a spurious kernel notification.
        bool ok = mtx.try_lock();
        REQUIRE( ok );
        if ( !ok )
            return;
        mtx.unlock();
    }
}

TEMPLATE_TEST_CASE( "async_waiter_guard: no stray notification after try_acquire cycle",
                    "[async_waiter_guard]",
                    NOVA_SYNC_ASYNC_MUTEX_TYPES )
{
    // try_acquire() lock-identity limitation — see "try_acquire succeeds" above.
    [ & ]() NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS {
        TestType mtx;

        const int rounds = 50;
        for ( int i = 0; i < rounds; ++i ) {
            nova::sync::detail::async_waiter_guard< TestType > guard( mtx );
            bool                                               acquired = guard.try_acquire();
            REQUIRE( acquired );
            mtx.unlock();
        }

        bool ok = mtx.try_lock();
        REQUIRE( ok );
        if ( !ok )
            return;
        REQUIRE_FALSE( mtx.try_lock() );
        mtx.unlock();
    }();
}

#endif // NOVA_SYNC_ASYNC_MUTEX_TYPES
