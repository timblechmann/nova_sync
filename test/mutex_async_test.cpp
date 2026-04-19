// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <catch2/catch_all.hpp>

#include <nova/sync/mutex/native_async_mutex.hpp>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <latch>
#include <thread>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helper: run an io_context on a background thread.
// ---------------------------------------------------------------------------

struct asio_runner
{
    boost::asio::io_context                                                    ioc;
    boost::asio::executor_work_guard< boost::asio::io_context::executor_type > work_guard {
        boost::asio::make_work_guard( ioc ) };
    std::thread thread { [ this ] {
        ioc.run();
    } };

    void stop()
    {
        work_guard.reset();
        if ( thread.joinable() )
            thread.join();
    }

    ~asio_runner()
    {
        stop();
    }
};

// ---------------------------------------------------------------------------
// Platform-specific async acquire helper (Boost.Asio)
// ---------------------------------------------------------------------------

#if defined( __linux__ ) || defined( __APPLE__ )

template < typename Mutex, typename Handler >
void async_acquire( boost::asio::io_context& ioc, Mutex& mtx, Handler on_acquired )
{
    if ( mtx.try_lock() ) {
        boost::asio::dispatch( ioc, std::move( on_acquired ) );
        return;
    }

    // Register as a waiter *before* the dup so unlock() sees the count.
    auto guard  = mtx.make_async_wait_guard();
    int  dup_fd = ::dup( guard.native_handle() );

    auto sd = std::make_shared< boost::asio::posix::stream_descriptor >( ioc, dup_fd );
    sd->async_wait( boost::asio::posix::stream_descriptor::wait_read,
                    [ &, sd, guard = std::move( guard ), on_acquired = std::move( on_acquired ) ](
                        const boost::system::error_code& ec ) mutable {
        if ( ec )
            return;
        // Destroy guard first: drains the notification and decrements the
        // waiter count before we retry, avoiding a spurious signal leak.
        // Even though the fd is now readable, that does not mean we own the
        // lock. Another thread may have woken up concurrently and acquired it,
        // or in a multi-waiter scenario, another thread may have already
        // consumed our notification. Thus we must call try_lock() again; if
        // it fails, we loop back and register a fresh guard.
        guard = typename Mutex::async_wait_guard { nullptr };
        async_acquire( ioc, mtx, std::move( on_acquired ) );
    } );
}

#elif defined( _WIN32 )

#    include <nova/sync/mutex/win32_mutex.hpp>

template < typename Mutex, typename Handler >
void async_acquire( boost::asio::io_context& ioc, Mutex& mtx, Handler on_acquired )
{
    if ( mtx.try_lock() ) {
        boost::asio::post( ioc, std::move( on_acquired ) );
        return;
    }

    auto   guard      = mtx.make_async_wait_guard();
    HANDLE dup_handle = nullptr;
    ::DuplicateHandle( ::GetCurrentProcess(),
                       static_cast< HANDLE >( guard.native_handle() ),
                       ::GetCurrentProcess(),
                       &dup_handle,
                       0,
                       FALSE,
                       DUPLICATE_SAME_ACCESS );

    auto oh = std::make_shared< boost::asio::windows::object_handle >( ioc, dup_handle );
    oh->async_wait( [ &, oh, guard = std::move( guard ), on_acquired = std::move( on_acquired ) ](
                        const boost::asio::error_code& ec ) mutable {
        if ( ec )
            return;
        // Destroy guard first: the kernel handle signal should not leak.
        // Just as on POSIX: fd readability does not imply lock ownership.
        // Another thread may have acquired the lock, or in a multi-waiter
        // scenario, another thread consumed the signal. We must retry.
        guard = typename Mutex::async_wait_guard { nullptr };
        async_acquire( ioc, mtx, std::move( on_acquired ) );
    } );
}

#endif // platform

// ---------------------------------------------------------------------------
// Boost.Asio tests
// ---------------------------------------------------------------------------

TEST_CASE( "native_async_mutex: async acquire fires after unlock", "[native_async_mutex][boost_asio]" )
{
    nova::sync::native_async_mutex mtx;
    asio_runner                    runner;

    std::atomic< bool > acquired_after_release { false };
    std::atomic< bool > mutex_released { false };

    mtx.lock();

    async_acquire( runner.ioc, mtx, [ & ] {
        acquired_after_release = mutex_released.load();
        mtx.unlock();
    } );

    REQUIRE( !mutex_released );

    std::this_thread::sleep_for( 30ms );
    mutex_released = true;
    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !acquired_after_release && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( acquired_after_release.load() );
}

TEST_CASE( "native_async_mutex: no early wakeup while locked", "[native_async_mutex][boost_asio]" )
{
    nova::sync::native_async_mutex mtx;
    asio_runner                    runner;

    std::atomic< bool > handler_fired { false };

    mtx.lock();

    async_acquire( runner.ioc, mtx, [ & ] {
        handler_fired = true;
        mtx.unlock();
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !handler_fired.load() );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_fired && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    runner.stop();

    REQUIRE( handler_fired.load() );
}

TEST_CASE( "native_async_mutex: mutual exclusion with async acquires", "[native_async_mutex][boost_asio]" )
{
    nova::sync::native_async_mutex mtx;
    asio_runner                    runner;

    const int          tasks = 8;
    std::atomic< int > inside { 0 };
    std::atomic< int > max_concurrent { 0 };
    std::atomic< int > completions { 0 };

    for ( int i = 0; i < tasks; ++i )
        boost::asio::post( runner.ioc, [ & ] {
            async_acquire( runner.ioc, mtx, [ & ] {
                int current  = ++inside;
                int expected = max_concurrent.load();
                while ( current > expected && !max_concurrent.compare_exchange_weak( expected, current ) )
                    ;
                std::this_thread::sleep_for( 1ms );
                --inside;
                mtx.unlock();
                ++completions;
            } );
        } );

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions.load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 10ms );

    runner.stop();

    REQUIRE( completions.load() == tasks );
    REQUIRE( max_concurrent.load() <= 1 );
}

TEST_CASE( "native_async_mutex: satisfies native_async_mutex concept", "[native_async_mutex]" )
{
    static_assert( nova::sync::concepts::native_async_mutex< nova::sync::native_async_mutex >,
                   "native_async_mutex does not satisfy native_async_mutex concept" );
    SUCCEED();
}

// ---------------------------------------------------------------------------
// libdispatch tests (Apple only)
// ---------------------------------------------------------------------------

#if defined( __APPLE__ )

#    include <dispatch/dispatch.h>

static void dispatch_async_acquire( nova::sync::native_async_mutex& mtx,
                                    dispatch_queue_t                queue,
                                    std::function< void() >         on_acquired )
{
    if ( mtx.try_lock() ) {
        // Fast path: already unlocked. Post the acquired handler to the queue.
        dispatch_async( queue, [ on_acquired = std::move( on_acquired ) ] {
            on_acquired();
        } );
        return;
    }

    // Slow path: register as an async waiter.
    using Guard = nova::sync::native_async_mutex::async_wait_guard;

    auto guard = std::make_shared< Guard >( mtx.make_async_wait_guard() );

    dispatch_source_t src = dispatch_source_create( DISPATCH_SOURCE_TYPE_READ, guard->native_handle(), 0, queue );

    dispatch_source_set_event_handler(
        src, [ guard = std::move( guard ), &mtx, src, queue, on_acquired = std::move( on_acquired ) ]() mutable {
        dispatch_source_cancel( src );
        dispatch_release( src );

        // Destroy the guard: this drains the kqueue notification and decrements
        // the waiter count. This must happen before we call try_lock() again,
        // so that if unlock() fires again while we're re-registering, there's
        // no stale signal waiting.
        guard.reset();

        // Explicit retry: fd readability does not mean we own the lock.
        // Call try_lock() again; if contended, register a fresh guard.
        dispatch_async_acquire( mtx, queue, std::move( on_acquired ) );
    } );

    dispatch_resume( src );
}

TEST_CASE( "native_async_mutex: libdispatch — async acquire fires after unlock", "[native_async_mutex][libdispatch]" )
{
    nova::sync::native_async_mutex mtx;
    dispatch_queue_t               queue = dispatch_queue_create( "nova.mutex.test", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > acquired_after_release { false };
    std::atomic< bool > mutex_released { false };
    std::atomic< bool > done { false };

    mtx.lock();

    dispatch_async_acquire( mtx, queue, [ & ] {
        acquired_after_release = mutex_released.load();
        mtx.unlock();
        done = true;
    } );

    std::this_thread::sleep_for( 30ms );
    mutex_released = true;
    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !done && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    dispatch_release( queue );

    REQUIRE( done.load() );
    REQUIRE( acquired_after_release.load() );
}

TEST_CASE( "native_async_mutex: libdispatch — no early wakeup while locked", "[native_async_mutex][libdispatch]" )
{
    nova::sync::native_async_mutex mtx;
    dispatch_queue_t               queue = dispatch_queue_create( "nova.mutex.test2", DISPATCH_QUEUE_SERIAL );

    std::atomic< bool > handler_fired { false };

    mtx.lock();

    dispatch_async_acquire( mtx, queue, [ & ] {
        handler_fired = true;
        mtx.unlock();
    } );

    std::this_thread::sleep_for( 60ms );
    REQUIRE( !handler_fired.load() );

    mtx.unlock();

    auto deadline = std::chrono::steady_clock::now() + 2s;
    while ( !handler_fired && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 5ms );

    dispatch_release( queue );

    REQUIRE( handler_fired.load() );
}

TEST_CASE( "native_async_mutex: libdispatch — mutual exclusion with multiple sources",
           "[native_async_mutex][libdispatch]" )
{
    nova::sync::native_async_mutex mtx;
    dispatch_queue_t               queue = dispatch_queue_create( "nova.mutex.test3", DISPATCH_QUEUE_CONCURRENT );

    const int tasks = 6;

    std::atomic< int > inside { 0 };
    std::atomic< int > max_concurrent { 0 };
    std::atomic< int > completions { 0 };

    for ( int i = 0; i < tasks; ++i ) {
        dispatch_async_acquire( mtx, queue, [ & ] {
            int current  = ++inside;
            int expected = max_concurrent.load();
            while ( current > expected && !max_concurrent.compare_exchange_weak( expected, current ) )
                ;
            std::this_thread::sleep_for( 1ms );
            --inside;
            mtx.unlock();
            ++completions;
        } );
    }

    auto deadline = std::chrono::steady_clock::now() + 5s;
    while ( completions.load() < tasks && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( 10ms );

    dispatch_release( queue );

    REQUIRE( completions.load() == tasks );
    REQUIRE( max_concurrent.load() <= 1 );
}

#endif // __APPLE__
