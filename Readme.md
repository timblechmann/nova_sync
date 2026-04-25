# nova::sync

Synchronization primitives for C++20: specialized mutex types optimized for use cases.

## Mutex Types

| Type | Characteristics | Named Requirement |
|------|-----------------|-------------------|
| `spinlock_mutex` | Simple spinlock | `Mutex` |
| `recursive_spinlock_mutex` | Recursive spinlock  | `Mutex` |
| `pthread_spinlock_mutex` | `pthread_spinlock_t` based spinlock  | `Mutex` |
| `shared_spinlock_mutex` | Shared spinlock | `SharedMutex` |
| `fast_mutex` | Fast general purpose mutex | `Mutex` |
| `fair_mutex` | Ticket lock, FIFO fairness guaranteed | `Mutex` |
| `pthread_priority_ceiling_mutex` | POSIX real-time mutex (PTHREAD_PRIO_PROTECT), Linux/POSIX only | `TimedMutex` |
| `pthread_priority_inherit_mutex` | POSIX real-time mutex (PTHREAD_PRIO_INHERIT), Linux/POSIX only | `TimedMutex` |
| `win32_recursive_mutex` | Win32 CRITICAL_SECTION, Windows only | `Mutex` |
| `win32_mutex` | Win32 kernel mutex, async-capable, Windows only | `TimedMutex` |
| `win32_srw_mutex` | Win32 SRW lock (ultra-lightweight), Windows only | `Mutex` |
| `apple_os_unfair_mutex` | Apple `os_unfair_lock`, macOS/iOS only | `Mutex` |
| `kqueue_mutex` | Apple kqueue-based async mutex, macOS/iOS only | `Mutex` |
| `eventfd_mutex` | Linux eventfd-based async mutex | `Mutex` |
| `native_async_mutex` | Cross-platform alias: `win32_mutex` / `kqueue_mutex` / `eventfd_mutex` | `Mutex` |

### `fast_mutex`

Lock-free implementation using `std::atomic::wait()`. Offers superior performance to `std::mutex` for contention-free and
moderately-contended scenarios.

### `fair_mutex`

Ticket lock implementation guaranteeing FIFO lock acquisition order. Prevents starvation and provides predictable fair
scheduling under high contention.

### POSIX real-time mutexes

Priority ceiling and inheritance protocols prevent priority inversion by temporarily boosting the lock holder's priority
to the highest waiter's level. Significantly higher locking overhead; suitable only for real-time systems where priority
inversion avoidance is critical.

### Platform-specific async mutexes

`win32_mutex`, `kqueue_mutex`, and `eventfd_mutex` (and their cross-platform alias `native_async_mutex`)
expose native OS handles (Win32 `HANDLE`, kqueue fd, eventfd respectively) enabling integration with event loops
(Boost.Asio, libdispatch, epoll, Qt, etc.).

The ready-made integration headers use the following handler convention:
The ready-made integration headers use an "expected"-style handler
convention: handlers receive an `expected<std::unique_lock<Mutex>,
std::error_code>` (`std::expected` or `tl::expected`). On success the
expected holds a locked `unique_lock`; on error it holds the error code.

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


### Benchmarks

Benchmarks for the mutex implementations (graphs are SVGs stored in the `benchmarks/` directory).
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


## Event Types

Event primitives for thread synchronization. There are variants for manual-reset and auto-reset semantics, with optional support for timed waits.

A manual-reset event is either *set* or *not set*. When set, every waiting thread is woken. The event stays set until `reset()` is called explicitly.

An auto-reset event delivers exactly one token per `signal()` call. Each token is consumed by exactly one `wait()` / `try_wait()` caller. Successive
`signal()` calls without an intervening `wait()` are coalesced when no waiters are registered.

| Type | Timed waits | Reset |
|------|-------------|-----------|
| `manual_reset_event` | - | Manual |
| `timed_manual_reset_event` | `wait_for` / `wait_until` | Manual |
| `native_manual_reset_event`| `wait_for` / `wait_until` | Manual |
| `auto_reset_event` | - | Automatic |
| `timed_auto_reset_event` | `wait_for` / `wait_until` | Automatic |
| `native_auto_reset_event`  | `wait_for` / `wait_until` | Automatic |

The `native_*` variants map directly to OS-specific synchronization primitives (`eventfd` on Linux, `kqueue` on macOS, `SetEvent` on Windows) and expose their underlying OS handle via `native_handle()`. This makes them ideal for integration with non-blocking event loops, C++20 coroutines, or C++26 executors.


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
