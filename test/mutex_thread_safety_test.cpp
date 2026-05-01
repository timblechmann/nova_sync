// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/parking_mutex.hpp>
#include <nova/sync/mutex/spinlock_mutex.hpp>
#include <nova/sync/mutex/ticket_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

#include <mutex>
#include <shared_mutex>

#include "mutex_types.hpp"

// ---------------------------------------------------------------------------
// Helpers used across all test cases
// ---------------------------------------------------------------------------

template < typename Mutex >
struct annotated_counter
{
    mutable Mutex mtx;
    int value     NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

    void increment() NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        ++value;
        mtx.unlock();
    }

    int get() const NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        int v = value;
        mtx.unlock();
        return v;
    }

    // Functions that require the caller to hold the lock
    void increment_locked() NOVA_SYNC_REQUIRES( mtx )
    {
        ++value;
    }

    int get_locked() const NOVA_SYNC_REQUIRES( mtx )
    {
        return value;
    }
};

template < typename Mutex >
struct annotated_ptr_container
{
    mutable Mutex mtx;
    int* ptr      NOVA_SYNC_PT_GUARDED_BY( mtx ) { nullptr };

    void set_value( int* p ) NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        ptr = p;
        mtx.unlock();
    }

    void write_through( int v ) NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        if ( ptr )
            *ptr = v;
        mtx.unlock();
    }

    int read_through() const NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        int v = ptr ? *ptr : 0;
        mtx.unlock();
        return v;
    }
};

// ---------------------------------------------------------------------------
// Scoped lock helper for REQUIRES tests
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Test cases — parametrised over mutex types
// ---------------------------------------------------------------------------

TEMPLATE_TEST_CASE( "thread-safety annotations: GUARDED_BY / EXCLUDES",
                    "[annotations][thread-safety]",
                    NOVA_SYNC_ALL_MUTEX_TYPES )
{
    annotated_counter< TestType > counter;

    SECTION( "increment and get via annotated wrapper" )
    {
        counter.increment();
        counter.increment();
        REQUIRE( counter.get() == 2 );
    }
}

TEMPLATE_TEST_CASE( "thread-safety annotations: REQUIRES", "[annotations][thread-safety]", NOVA_SYNC_ALL_MUTEX_TYPES )
{
    annotated_counter< TestType > counter;

    SECTION( "call REQUIRES function while holding lock via scoped guard" )
    {
        nova::sync::lock_guard< TestType > guard( counter.mtx );
        counter.increment_locked();
        counter.increment_locked();
        REQUIRE( counter.get_locked() == 2 );
        // guard releases on scope exit
    }
}

TEMPLATE_TEST_CASE( "thread-safety annotations: PT_GUARDED_BY", "[annotations][thread-safety]", NOVA_SYNC_ALL_MUTEX_TYPES )
{
    annotated_ptr_container< TestType > container;
    int                                 value = 0;

    SECTION( "set pointer and write/read through annotated container" )
    {
        container.set_value( &value );
        container.write_through( 42 );
        REQUIRE( container.read_through() == 42 );
    }
}

// shared_spinlock_mutex — test shared capability annotations
TEST_CASE( "thread-safety annotations: shared mutex GUARDED_BY", "[annotations][thread-safety]" )
{
    struct shared_counter
    {
        mutable nova::sync::shared_spinlock_mutex<> mtx;
        int value                                   NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void write( int v ) NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            value = v;
            mtx.unlock();
        }

        int read() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock_shared();
            int v = value;
            mtx.unlock_shared();
            return v;
        }
    };

    shared_counter c;
    c.write( 7 );
    REQUIRE( c.read() == 7 );
}

// recursive_spinlock_mutex — ensure REENTRANT_CAPABILITY compiles without warnings
TEST_CASE( "thread-safety annotations: recursive mutex REENTRANT_CAPABILITY", "[annotations][thread-safety]" )
{
    struct recursive_counter
    {
        mutable nova::sync::recursive_spinlock_mutex<> mtx;
        int value                                      NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment()
#if defined( __clang__ ) && ( __clang_major__ >= 22 )
            NOVA_SYNC_EXCLUDES( mtx )
#else
            NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
#endif
        {
            mtx.lock();
            ++value;
            mtx.unlock();
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            int v = value;
            mtx.unlock();
            return v;
        }
    };

    recursive_counter c;
    c.increment();
    c.increment();
    REQUIRE( c.get() == 2 );
}

// ---------------------------------------------------------------------------
// tsa_mutex_adapter tests
// ---------------------------------------------------------------------------

TEST_CASE( "tsa_mutex_adapter: wraps std::mutex with GUARDED_BY", "[annotations][tsa_mutex_adapter]" )
{
    struct guarded
    {
        mutable nova::sync::tsa_mutex_adapter< std::mutex > mtx;
        int value                                           NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            ++value;
            mtx.unlock();
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            int v = value;
            mtx.unlock();
            return v;
        }
    };

    guarded g;
    g.increment();
    g.increment();
    REQUIRE( g.get() == 2 );
}

TEST_CASE( "tsa_mutex_adapter: try_lock forwarded for std::mutex", "[annotations][tsa_mutex_adapter]" )
{
    nova::sync::tsa_mutex_adapter< std::mutex > mtx;
    bool                                        locked = mtx.try_lock();
    REQUIRE( locked );
    if ( !locked )
        return;
    mtx.unlock();
}

TEST_CASE( "tsa_mutex_adapter: wraps std::timed_mutex", "[annotations][tsa_mutex_adapter]" )
{
    nova::sync::tsa_mutex_adapter< std::timed_mutex > mtx;
    bool                                              locked_for = mtx.try_lock_for( std::chrono::milliseconds( 1 ) );
    REQUIRE( locked_for );
    if ( locked_for )
        mtx.unlock();

    bool locked_until = mtx.try_lock_until( std::chrono::steady_clock::now() + std::chrono::milliseconds( 1 ) );
    REQUIRE( locked_until );
    if ( locked_until )
        mtx.unlock();
}

TEST_CASE( "tsa_mutex_adapter: wraps std::shared_mutex", "[annotations][tsa_mutex_adapter]" )
{
    nova::sync::tsa_mutex_adapter< std::shared_mutex > mtx;

    SECTION( "exclusive lock" )
    {
        mtx.lock();
        mtx.unlock();
    }

    SECTION( "shared lock" )
    {
        mtx.lock_shared();
        mtx.unlock_shared();
    }
}

TEST_CASE( "tsa_mutex_adapter: try_lock_shared forwarded for std::shared_mutex", "[annotations][tsa_mutex_adapter]" )
{
    nova::sync::tsa_mutex_adapter< std::shared_mutex > mtx;
    bool                                               locked = mtx.try_lock_shared();
    REQUIRE( locked );
    if ( !locked )
        return;
    mtx.unlock_shared();
}

TEST_CASE( "tsa_mutex_adapter: wraps nova::sync::parking_mutex with GUARDED_BY", "[annotations][tsa_mutex_adapter]" )
{
    struct guarded
    {
        mutable nova::sync::tsa_mutex_adapter< nova::sync::parking_mutex<> > mtx;
        int value                                                            NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            ++value;
            mtx.unlock();
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            int v = value;
            mtx.unlock();
            return v;
        }
    };

    guarded g;
    g.increment();
    REQUIRE( g.get() == 1 );
}

// ---------------------------------------------------------------------------
// lock_guard tests
// ---------------------------------------------------------------------------

TEST_CASE( "lock_guard: acquires and releases", "[annotations][lock_guard]" )
{
    struct guarded
    {
        mutable nova::sync::spinlock_mutex<> mtx;
        int value                            NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            nova::sync::lock_guard< nova::sync::spinlock_mutex<> > guard( mtx );
            ++value;
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            nova::sync::lock_guard< nova::sync::spinlock_mutex<> > guard( mtx );
            return value;
        }
    };

    guarded g;
    g.increment();
    g.increment();
    REQUIRE( g.get() == 2 );
}

TEST_CASE( "lock_guard: adopt_lock does not re-lock", "[annotations][lock_guard]" )
{
    nova::sync::spinlock_mutex<> mtx;
    mtx.lock();
    {
        nova::sync::lock_guard< nova::sync::spinlock_mutex<> > guard( mtx, std::adopt_lock );
    }
    // mutex is now unlocked — we can lock again
    bool locked = mtx.try_lock();
    REQUIRE( locked );
    if ( locked )
        mtx.unlock();
}

TEST_CASE( "lock_guard: works with tsa_mutex_adapter", "[annotations][lock_guard]" )
{
    nova::sync::tsa_mutex_adapter< std::mutex > mtx;
    {
        nova::sync::lock_guard< nova::sync::tsa_mutex_adapter< std::mutex > > guard( mtx );
    }
    // lock must be released; lock again to verify
    mtx.lock();
    mtx.unlock();
}

#if defined( __clang__ ) && ( __clang_major__ >= 22 )

TEST_CASE( "tsa_recursive_mutex_adapter: wraps recursive_spinlock_mutex", "[annotations][tsa_recursive_mutex_adapter]" )
{
    struct guarded
    {
        mutable nova::sync::tsa_recursive_mutex_adapter< nova::sync::recursive_spinlock_mutex<> > mtx;
        int value NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            ++value;
            mtx.unlock();
        }

        void increment_recursively() NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            mtx.lock();
            ++value;
            mtx.unlock();
            mtx.unlock();
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            int v = value;
            mtx.unlock();
            return v;
        }
    };

    guarded g;
    g.increment();
    g.increment();
    REQUIRE( g.get() == 2 );
}

TEST_CASE( "tsa_recursive_mutex_adapter: wraps std::recursive_mutex", "[annotations][tsa_recursive_mutex_adapter]" )
{
    struct guarded
    {
        mutable nova::sync::tsa_recursive_mutex_adapter< std::recursive_mutex > mtx;
        int value                                                               NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            ++value;
            mtx.unlock();
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            int v = value;
            mtx.unlock();
            return v;
        }
    };

    guarded g;
    g.increment();
    g.increment();
    REQUIRE( g.get() == 2 );
}

#endif

TEST_CASE( "thread-safety annotations: ACQUIRED_BEFORE lock ordering", "[annotations][thread-safety]" )
{
    struct double_guarded
    {
        mutable nova::sync::spinlock_mutex<> mtx1 NOVA_SYNC_ACQUIRED_BEFORE( mtx2 );
        mutable nova::sync::spinlock_mutex<>      mtx2;

        int value1 NOVA_SYNC_GUARDED_BY( mtx1 ) { 1 };
        int value2 NOVA_SYNC_GUARDED_BY( mtx2 ) { 2 };

        int get_sum() const NOVA_SYNC_EXCLUDES( mtx1, mtx2 )
        {
            mtx1.lock();
            mtx2.lock(); // Statically checks proper ordering mtx1 -> mtx2
            int sum = value1 + value2;
            mtx2.unlock();
            mtx1.unlock();
            return sum;
        }
    };

    double_guarded g;
    REQUIRE( g.get_sum() == 3 );
}

TEST_CASE( "thread-safety annotations: ASSERT_CAPABILITY", "[annotations][thread-safety]" )
{
    struct runtime_checked
    {
        mutable nova::sync::spinlock_mutex<> mtx;
        int value                            NOVA_SYNC_GUARDED_BY( mtx ) { 42 };

        void assert_is_locked() const NOVA_SYNC_ASSERT_CAPABILITY( mtx )
        {
            // In a real scenario, this would have a runtime check
            // verifying that the current thread owns the mutex.
        }

        int get_with_assert() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            assert_is_locked(); // Analyzer now considers 'mtx' held even from this point forward
            int v = value;
            mtx.unlock();
            return v;
        }
    };

    runtime_checked c;
    REQUIRE( c.get_with_assert() == 42 );
}

TEST_CASE( "thread-safety annotations: ACQUIRED_AFTER lock ordering", "[annotations][thread-safety]" )
{
    struct double_guarded
    {
        mutable nova::sync::spinlock_mutex<>      mtx1;
        mutable nova::sync::spinlock_mutex<> mtx2 NOVA_SYNC_ACQUIRED_AFTER( mtx1 );

        int value1 NOVA_SYNC_GUARDED_BY( mtx1 ) { 1 };
        int value2 NOVA_SYNC_GUARDED_BY( mtx2 ) { 2 };

        int get_sum() const NOVA_SYNC_EXCLUDES( mtx1, mtx2 )
        {
            mtx1.lock();
            mtx2.lock(); // Statically checks proper ordering mtx1 -> mtx2
            int sum = value1 + value2;
            mtx2.unlock();
            mtx1.unlock();
            return sum;
        }
    };

    double_guarded g;
    REQUIRE( g.get_sum() == 3 );
}

TEST_CASE( "thread-safety annotations: shared capability with REQUIRES_SHARED", "[annotations][thread-safety]" )
{
    struct shared_guarded
    {
        mutable nova::sync::shared_spinlock_mutex<> mtx;
        int value                                   NOVA_SYNC_GUARDED_BY( mtx ) { 99 };

        int read_shared() const NOVA_SYNC_REQUIRES_SHARED( mtx )
        {
            return value;
        }

        void write_exclusive() NOVA_SYNC_REQUIRES( mtx )
        {
            ++value;
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock_shared();
            int v = read_shared();
            mtx.unlock_shared();
            return v;
        }

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            mtx.lock();
            write_exclusive();
            mtx.unlock();
        }
    };

    shared_guarded g;
    g.increment();
    REQUIRE( g.get() == 100 );
}

TEST_CASE( "tsa_mutex_adapter: try_lock_until forwarded for std::timed_mutex", "[annotations][tsa_mutex_adapter]" )
{
    nova::sync::tsa_mutex_adapter< std::timed_mutex > mtx;
    bool locked_until = mtx.try_lock_until( std::chrono::steady_clock::now() + std::chrono::milliseconds( 1 ) );
    REQUIRE( locked_until );
    if ( locked_until )
        mtx.unlock();
}

TEST_CASE( "lock_guard: works with shared_spinlock_mutex", "[annotations][lock_guard]" )
{
    struct guarded_shared
    {
        mutable nova::sync::shared_spinlock_mutex<> mtx;
        int value                                   NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

        void increment() NOVA_SYNC_EXCLUDES( mtx )
        {
            nova::sync::lock_guard< nova::sync::shared_spinlock_mutex<> > guard( mtx );
            ++value;
        }

        int get() const NOVA_SYNC_EXCLUDES( mtx )
        {
            nova::sync::lock_guard< nova::sync::shared_spinlock_mutex<> > guard( mtx );
            return value;
        }
    };

    guarded_shared g;
    g.increment();
    REQUIRE( g.get() == 1 );
}
