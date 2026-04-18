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
