// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/annotations.hpp>
#include <nova/sync/mutex/fair_mutex.hpp>
#include <nova/sync/mutex/fast_mutex.hpp>
#include <nova/sync/mutex/recursive_spinlock_mutex.hpp>
#include <nova/sync/mutex/shared_spinlock_mutex.hpp>
#include <nova/sync/mutex/spinlock_mutex.hpp>

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

template < typename Mutex >
class NOVA_SYNC_SCOPED_CAPABILITY scoped_lock
{
public:
    explicit scoped_lock( Mutex& m ) NOVA_SYNC_ACQUIRE( m ) :
        m_( m )
    {
        m_.lock();
    }
    ~scoped_lock() NOVA_SYNC_RELEASE()
    {
        m_.unlock();
    }

    scoped_lock( const scoped_lock& )            = delete;
    scoped_lock& operator=( const scoped_lock& ) = delete;

private:
    Mutex& m_;
};

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
        scoped_lock guard( counter.mtx );
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
        mutable nova::sync::shared_spinlock_mutex mtx;
        int value                                 NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

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
        mutable nova::sync::recursive_spinlock_mutex mtx;
        int value                                    NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

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

    recursive_counter c;
    c.increment();
    c.increment();
    REQUIRE( c.get() == 2 );
}
