# nova::sync

Synchronization primitives for C++20: specialized mutex and event types optimized for different use cases.

## Mutex Types

| Type | Characteristics | Named Requirement |
|------|-----------------|-------------------|
| `parking_mutex<>` | Futex-based mutex, parks immediately | `Mutex` |
| `parking_mutex<with_backoff>` | Futex-based mutex, exponential backoff before parking | `Mutex` |
| `parking_mutex<timed>` | Futex-based mutex, no spin, timed waits | `TimedMutex` |
| `parking_mutex<timed, with_backoff>` | Futex-based mutex, exponential backoff, timed waits | `TimedMutex` |
| `ticket_mutex<>` | Fair FIFO ticket lock, futex sleep | `TimedMutex` |
| `ticket_mutex<with_backoff>` | Fair FIFO ticket lock with exponential backoff | `TimedMutex` |
| `spinlock_mutex<>` | Spinlock, CPU-pause hints | `Mutex` |
| `spinlock_mutex<with_backoff>` | Spinlock, exponential backoff | `Mutex` |
| `spinlock_mutex<recursive>` | Recursive spinlock | `Mutex` |
| `spinlock_mutex<recursive, with_backoff>` | Recursive spinlock with backoff | `Mutex` |
| `spinlock_mutex<shared>` | Shared (reader-writer) spinlock | `SharedMutex` |
| `spinlock_mutex<shared, with_backoff>` | Shared spinlock with exponential backoff | `SharedMutex` |
| `pthread_spinlock_mutex` | `pthread_spinlock_t` based spinlock, POSIX only | `Mutex` |
| `pthread_mutex<>` | POSIX `pthread_mutex_t` (default type) | `TimedMutex` |
| `pthread_mutex<pthread_recursive>` | Recursive POSIX mutex | `TimedMutex` |
| `pthread_mutex<pthread_errorcheck>` | Error-checking POSIX mutex | `TimedMutex` |
| `pthread_mutex<pthread_adaptive>` | Adaptive-spin POSIX mutex (Linux) | `TimedMutex` |
| `pthread_mutex<priority_inherit>` | POSIX mutex, priority inheritance (RT) | `TimedMutex` |
| `pthread_mutex<priority_ceiling<N>>` | POSIX mutex, priority ceiling N (RT) | `TimedMutex` |
| `win32_critical_section_mutex<>` | Win32 CRITICAL_SECTION, recursive, Windows only | `Mutex` |
| `win32_critical_section_mutex<win32_spin_count<N>>` | Win32 CRITICAL_SECTION with custom spin count | `Mutex` |
| `win32_mutex` | Win32 kernel mutex, async-capable, Windows only | `TimedMutex` |
| `win32_srw_mutex` | Win32 SRW lock (ultra-lightweight), Windows only | `Mutex` |
| `apple_os_unfair_mutex` | Apple `os_unfair_lock`, macOS/iOS only | `Mutex` |
| `kqueue_mutex<>` | Apple kqueue-based async mutex | `TimedMutex` |
| `kqueue_mutex<with_backoff>` | kqueue mutex with exponential backoff | `TimedMutex` |
| `eventfd_mutex<>` | Linux eventfd-based async mutex | `TimedMutex` |
| `eventfd_mutex<with_backoff>` | eventfd mutex with exponential backoff | `TimedMutex` |
| `native_async_mutex` | Cross-platform alias: `win32_event_mutex` / `kqueue_mutex<with_backoff>` / `eventfd_mutex<with_backoff>` | `TimedMutex` |

### Policy parameters

All policy types live in `nova/sync/mutex/policies.hpp`:

| Policy | Effect |
|--------|--------|
| `with_backoff` | Exponential backoff with CPU pause hints before blocking |
| `recursive` | Allow re-entrant locking from the owning thread (`spinlock_mutex` only) |
| `shared` | Enable shared (reader-writer) locking via `lock_shared()` (`spinlock_mutex` only; mutually exclusive with `recursive`) |
| `priority_inherit` | PTHREAD_PRIO_INHERIT — owner boosted to highest waiter priority |
| `priority_ceiling<N>` | PTHREAD_PRIO_PROTECT — all holders elevated to ceiling N |
| `pthread_recursive` | PTHREAD_MUTEX_RECURSIVE — re-entrant locking |
| `pthread_errorcheck` | PTHREAD_MUTEX_ERRORCHECK — error on double-lock |
| `pthread_adaptive` | PTHREAD_MUTEX_ADAPTIVE_NP — adaptive spin (Linux only) |
| `win32_spin_count<N>` | Spin count for `InitializeCriticalSectionAndSpinCount` |

### Convenience aliases

```cpp
using pthread_default_mutex          = pthread_mutex<>;
using pthread_recursive_mutex        = pthread_mutex< pthread_recursive >;
using pthread_priority_inherit_mutex = pthread_mutex< priority_inherit >;
template < int N >
using pthread_priority_ceiling_mutex = pthread_mutex< priority_ceiling< N > >;
```

### `parking_mutex`

Futex-based mutex using `std::atomic::wait()`. Fast path acquires in one CAS; slow path parks the calling thread. With `with_backoff`, spins briefly before parking — lower latency under brief contention. Add `timed` to enable `try_lock_for` / `try_lock_until`.

### `ticket_mutex`

FIFO ticket lock guaranteeing strict acquisition order. Prevents starvation under sustained contention. Not suitable for high-throughput low-contention workloads.

### POSIX mutexes

`pthread_mutex<>` wraps `pthread_mutex_t`. Priority-protocol variants (`priority_inherit`, `priority_ceiling<N>`) prevent priority inversion in real-time systems — higher overhead; requires RT scheduling for ceiling variant.

### Platform-specific async mutexes

`win32_event_mutex`, `kqueue_mutex`, `eventfd_mutex` (and their `<with_backoff>` policy variants)
expose native OS handles enabling integration with event loops (Boost.Asio, libdispatch, epoll, Qt, etc.).
The `native_async_mutex` alias resolves to the fastest variant (`with_backoff`) for the current platform.

Handlers receive an `expected<std::unique_lock<Mutex>, std::error_code>` (`std::expected` or `tl::expected`):

```cpp
void handler(expected<std::unique_lock<Mutex>, std::error_code> result);
```

**Boost.Asio — callback:**

```cpp
#include <nova/sync/mutex/boost_asio_support.hpp>

nova::sync::native_async_mutex mtx;
boost::asio::io_context        ioc;

// Non-cancellable:
nova::sync::async_acquire(ioc, mtx,
    [](auto result) {
        if (!result) return; // unexpected error
        auto& lock = *result; // lock.owns_lock() == true — critical section here
        // lock releases the mutex automatically on scope exit
    });

// Cancellable:
auto handle = nova::sync::async_acquire_cancellable(ioc, mtx,
    [](auto result) {
        if (!result) {
            if (result.error() == std::errc::operation_canceled) return; // cancelled
            return; // other error
        }
        auto& lock = *result; // lock.owns_lock() == true
    });
handle.cancel(); // abort the pending wait from any thread
```

**Boost.Asio — future:**

```cpp
#include <nova/sync/mutex/boost_asio_support.hpp>

nova::sync::native_async_mutex mtx;
boost::asio::io_context        ioc;

auto [descriptor, fut] = nova::sync::async_acquire(ioc, mtx);
// descriptor keeps the wait alive; fut becomes ready when the lock is acquired

std::unique_lock lock = fut.get(); // blocks until acquired; lock.owns_lock() == true

// To cancel: descriptor->cancel(); // fut will never become ready
```

### Thread Safety Analysis

 All mutex types are annotated for Clang's thread-safety analysis (`-Wthread-safety`). Macros in `<nova/sync/thread_safety/macros.hpp>` map to TSA attributes (e.g., `NOVA_SYNC_GUARDED_BY`, `NOVA_SYNC_REQUIRES`, `NOVA_SYNC_EXCLUDES`, `NOVA_SYNC_ACQUIRE`, `NOVA_SYNC_RELEASE`) on Clang and expand to nothing on other compilers.

**Typical usage:**
```cpp
nova::sync::spinlock_mutex mtx;
int counter NOVA_SYNC_GUARDED_BY(mtx);

void increment() NOVA_SYNC_REQUIRES(mtx) { counter++; }

{
    nova::sync::lock_guard lock(mtx);
    increment();  // OK: mtx held by lock_guard
}
increment();      // Error: mutex not held
```


### `locked_object<T, Mutex>` — Rust-inspired Thread-Safe Value Wrapper

Type-safe RAII wrapper pairing a value `T` with a `Mutex`, enforcing synchronized access at compile time. The value is only accessible through lock guards. Supports exclusive locking (mutual exclusion) and shared locking (read-write patterns with `std::shared_mutex` or compatible).

**Lock guards:**
- `locked_object_guard<T, M>`: Exclusive lock with constness determined by T template parameter
- `shared_locked_object_guard<T, M>`: Shared lock (read-lock) with const access; requires `std::shared_mutex`

**Key feature:** Const instances can acquire exclusive locks (enabling interior mutability patterns while maintaining thread safety).

```cpp
#include <nova/sync/thread_safety/locked_object.hpp>

nova::sync::locked_object< int > counter( 0 );

// Exclusive lock on non-const instance → mutable access
{
    auto guard = counter.lock();  // returns locked_object_guard<int, std::mutex>
    *guard = 42;
}

// Const instance can acquire exclusive lock → const access
const auto& const_counter = counter;
{
    auto guard = const_counter.lock();  // returns locked_object_guard<const int, std::mutex>
    // *guard = 42;  // compile error: const access
    int val = *guard;  // OK
}

// Try-lock (non-blocking)
if (auto guard = counter.try_lock()) {
    *guard += 1;
}

// Shared lock (requires std::shared_mutex)
nova::sync::locked_object< std::vector<int>, std::shared_mutex > data( {} );
{
    auto guard = data.lock_shared();  // returns shared_locked_object_guard<...>
    for (int x : *guard) { /* ... */ }  // concurrent readers allowed
}

// Higher-order: lock_and (acquire lock, invoke function, auto-release)
counter.lock_and( [](int& v) {
    v = 100;
} );

int squared = counter.lock_and( [](const int& v) {
    return v * v;
} );

// Try-lock_and (non-blocking higher-order)
auto updated = counter.try_lock_and( [](int& v) {
    v += 10;
    return v;
} );  // returns std::optional<int>

// Shared lock higher-order
int sum = data.lock_shared_and( [](const std::vector<int>& v) {
    int result = 0;
    for (int x : v) result += x;
    return result;
} );
```

### Benchmarks

The following results were recorded on Ubuntu 25.04 on an Intel i7-14700K.

#### Linux (Ubuntu 25.10) — Intel i7-14700K

Single-threaded benchmark:

![Linux single-threaded benchmark](benchmarks/linux_intel_14700K_single-threaded.svg)

Multi-threaded benchmark:

![Linux multi-threaded benchmark](benchmarks/linux_intel_14700K_multi-threaded.svg)

#### macOS - Apple M4 Pro

Single-threaded benchmark:

![macOS single-threaded](benchmarks/macos_m4_single-threaded.svg)

Multi-threaded benchmark:

![macOS multi-threaded](benchmarks/macos_m4_multi-threaded.svg)

#### Windows 11 — Intel i7-14700K

Single-threaded benchmark:

![Windows single-threaded benchmark](benchmarks/win32_intel_14700K_single-threaded.svg)

Multi-threaded benchmark:

![Windows multi-threaded benchmark](benchmarks/win32_intel_14700K_multi-threaded.svg)


## Semaphore Types

| Type | Timed waits | Native handle | Platform |
|------|-------------|---------------|----------|
| `fast_semaphore` | — | — | Cross-platform |
| `timed_counting_semaphore` | `try_acquire_for` / `try_acquire_until` | — | Cross-platform |
| `posix_semaphore` | `try_acquire_for` / `try_acquire_until` | — | Linux |
| `win32_semaphore` | `try_acquire_for` / `try_acquire_until` | `native_handle()` | Windows |
| `eventfd_semaphore` | `try_acquire_for` / `try_acquire_until` | `native_handle()` | Linux |
| `kqueue_semaphore` | `try_acquire_for` / `try_acquire_until` | `native_handle()` | macOS/iOS |
| `mach_semaphore` | `try_acquire_for` / `try_acquire_until` | — | macOS/iOS |
| `dispatch_semaphore` | `try_acquire_for` / `try_acquire_until` | — | macOS/iOS |
| `native_async_semaphore` | `try_acquire_for` / `try_acquire_until` | `native_handle()` | Platform-specific alias |

### Platform-specific async semaphores

`win32_semaphore`, `eventfd_semaphore`, `kqueue_semaphore`, and `mach_semaphore` wrap OS primitives and expose `native_handle()` for integration with event loops (Boost.Asio, libdispatch, epoll, Qt, etc.).

```cpp
nova::sync::counting_semaphore sem(0);

// Producer thread
sem.release(5);  // add 5 tokens

// Consumer thread
sem.acquire();   // block until token available; consumes one
if (sem.try_acquire())  // non-blocking; consumes if available
    // ... token acquired
```

Async integration (with `native_async_semaphore`):

```cpp
nova::sync::native_async_semaphore sem(0);

// Register for async notification when a token becomes available
auto handle = nova::sync::async_acquire_cancellable(ioc, sem,
    [](auto result) {
        if (result) {
            // Token acquired; use it
        } else if (result.error() == std::errc::operation_canceled) {
            // Wait was cancelled
        }
    });

handle.cancel();  // abort pending wait
```

## Event Types

| Type | Timed waits | Reset | Native handle |
|------|-------------|-------|---------------|
| `manual_reset_event` | — | Manual | — |
| `timed_manual_reset_event` | `try_wait_for` / `try_wait_until` | Manual | — |
| `native_manual_reset_event`| `try_wait_for` / `try_wait_until` | Manual | `native_handle()` |
| `auto_reset_event` | — | Automatic | — |
| `timed_auto_reset_event` | `try_wait_for` / `try_wait_until` | Automatic | — |
| `native_auto_reset_event`  | `try_wait_for` / `try_wait_until` | Automatic | `native_handle()` |

### Manual-reset events

Once `signal()` is called, all waiters are woken and subsequent `wait()` / `try_wait()` calls return immediately until `reset()` is called.

### Auto-reset events

Each `signal()` delivers exactly one token. A blocked waiter consumes it; otherwise the next `wait()` / `try_wait()` call consumes it.

### Native events

The `native_*` variants map to OS primitives (`eventfd` on Linux, `kqueue` on macOS, `SetEvent` on Windows) and expose `native_handle()` for integration with event loops, C++20 coroutines, or C++26 executors.

```cpp
nova::sync::manual_reset_event ev;

// Producer thread
ev.signal();          // wake all waiters; event stays set

// Consumer threads
ev.wait();            // block until set
ev.try_wait();        // non-blocking check
ev.reset();           // clear the event
```

```cpp
nova::sync::auto_reset_event ev;

// Producer thread
ev.signal();          // deliver one token

// Consumer thread
ev.wait();            // block until a token is available; consumes it
ev.try_wait();        // non-blocking; returns true and consumes token if available
```

## Dependencies

- C++20 (GCC 12+, Clang 17+, MSVC 2022+)
- No external dependencies for core library
- Tests require Catch2 and Boost.asio (fetched via CPM)

## Building

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

## License

MIT — see [License.txt](License.txt)
