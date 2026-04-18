# nova::sync

Synchronization primitives for C++20: specialized mutex types optimized for use cases.

## Mutex Types

| Type | Characteristics | Named Requirement |
|------|-----------------|-------------------|
| `spinlock_mutex` | Simple spinlock | `Mutex` |
| `recursive_spinlock_mutex` | Recursive spinlock  | `Mutex` |
| `shared_spinlock_mutex` | Shared spinlock | `SharedMutex` |
| `fast_mutex` | Fast general purpose mutex | `Mutex` |
| `fair_mutex` | Ticket lock, FIFO fairness guaranteed | `Mutex` |

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
- No external dependencies

## Building

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

## License

MIT — see [License.txt](License.txt)
