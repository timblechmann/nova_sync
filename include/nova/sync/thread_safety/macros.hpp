// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file tsa_macros.hpp
/// @brief Thread-safety analysis macro wrappers for Clang TSA.
///
/// These macros expand to Clang thread-safety analysis attributes when
/// supported (`-Wthread-safety`), and to nothing on other compilers.
///
/// **Capability Attributes** (declare and annotate synchronization primitives):
/// - `NOVA_SYNC_CAPABILITY(name)` — Declare a class as a lock/capability
/// - `NOVA_SYNC_SCOPED_LOCKABLE` — Mark a class as SCOPED_LOCKABLE (auto-releases
///   on scope exit, e.g., lock_guard, unique_lock)
/// - `NOVA_SYNC_REENTRANT_CAPABILITY` — Mark a capability as reentrant
///
/// **Lock Acquisition Attributes** (functions that acquire locks):
/// - `NOVA_SYNC_ACQUIRE(...)` — Acquire exclusive lock capability
/// - `NOVA_SYNC_ACQUIRE_SHARED(...)` — Acquire shared lock capability
/// - `NOVA_SYNC_TRY_ACQUIRE(...)` — Try to acquire exclusive lock (bool return)
/// - `NOVA_SYNC_TRY_ACQUIRE_SHARED(...)` — Try to acquire shared lock (bool return)
///
/// **Lock Release Attributes** (functions that release locks):
/// - `NOVA_SYNC_RELEASE(...)` — Release exclusive lock capability
/// - `NOVA_SYNC_RELEASE_SHARED(...)` — Release shared lock capability
/// - `NOVA_SYNC_RELEASE_GENERIC(...)` — Release any lock capability
///
/// **Precondition Attributes** (declare lock requirements on functions/variables):
/// - `NOVA_SYNC_REQUIRES(...)` — Requires exclusive lock on entry
/// - `NOVA_SYNC_REQUIRES_SHARED(...)` — Requires shared lock on entry
/// - `NOVA_SYNC_EXCLUDES(...)` — Requires lock NOT held on entry
/// - `NOVA_SYNC_GUARDED_BY(...)` — Variable guarded by lock
/// - `NOVA_SYNC_PT_GUARDED_BY(...)` — Pointer's pointee guarded by lock
///
/// **Lock Order Attributes** (enforce consistent lock acquisition order):
/// - `NOVA_SYNC_ACQUIRED_BEFORE(...)` — This lock must be acquired before other(s)
/// - `NOVA_SYNC_ACQUIRED_AFTER(...)` — This lock must be acquired after other(s)
///
/// **Assertion Attributes** (assert lock state at a point in code):
/// - `NOVA_SYNC_ASSERT_CAPABILITY(x)` — Assert capability held (exclusive or shared)
/// - `NOVA_SYNC_ASSERT_SHARED_CAPABILITY(x)` — Assert shared lock held
///
/// **Analysis Control**:
/// - `NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS` — Disable TSA for a function (use
///   sparingly: complex runtime logic, move semantics, conditionals)
///
/// **Example Usage**:
/// ```cpp
/// class spinlock_mutex {
///   NOVA_SYNC_CAPABILITY("mutex") private:
///   std::atomic_flag flag_;
/// public:
///   NOVA_SYNC_ACQUIRE(this) void lock() { /*...*/ }
///   NOVA_SYNC_RELEASE(this) void unlock() { /*...*/ }
/// };
///
/// class lock_guard {
///   NOVA_SYNC_SCOPED_LOCKABLE private:
///   spinlock_mutex& mtx_;
/// public:
///   explicit lock_guard(spinlock_mutex& m) NOVA_SYNC_ACQUIRE(m) : mtx_(m) { m.lock(); }
///   ~lock_guard() NOVA_SYNC_RELEASE() { mtx_.unlock(); }
/// };
///
/// int counter NOVA_SYNC_GUARDED_BY(mtx);
/// void increment() NOVA_SYNC_REQUIRES(mtx) { counter++; }
/// ```

#ifndef __has_attribute
#  define __has_attribute( x ) 0
#endif

#if __has_attribute( capability )
#  define NOVA_SYNC_CAPABILITY( x ) __attribute__( ( capability( x ) ) )
#else
#  define NOVA_SYNC_CAPABILITY( x )
#endif

#if __has_attribute( reentrant_capability )
#  define NOVA_SYNC_REENTRANT_CAPABILITY __attribute__( ( reentrant_capability ) )
#else
#  define NOVA_SYNC_REENTRANT_CAPABILITY
#endif

#if __has_attribute( scoped_lockable )
#  define NOVA_SYNC_SCOPED_LOCKABLE __attribute__( ( scoped_lockable ) )
#else
#  define NOVA_SYNC_SCOPED_LOCKABLE
#endif

#if __has_attribute( acquire_capability )
#  define NOVA_SYNC_ACQUIRE( ... ) __attribute__( ( acquire_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_ACQUIRE( ... )
#endif

#if __has_attribute( acquire_shared_capability )
#  define NOVA_SYNC_ACQUIRE_SHARED( ... ) __attribute__( ( acquire_shared_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_ACQUIRE_SHARED( ... )
#endif

#if __has_attribute( release_capability )
#  define NOVA_SYNC_RELEASE( ... ) __attribute__( ( release_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_RELEASE( ... )
#endif

#if __has_attribute( release_shared_capability )
#  define NOVA_SYNC_RELEASE_SHARED( ... ) __attribute__( ( release_shared_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_RELEASE_SHARED( ... )
#endif

#if __has_attribute( release_generic_capability )
#  define NOVA_SYNC_RELEASE_GENERIC( ... ) __attribute__( ( release_generic_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_RELEASE_GENERIC( ... )
#endif

#if __has_attribute( try_acquire_capability )
#  define NOVA_SYNC_TRY_ACQUIRE( ... ) __attribute__( ( try_acquire_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_TRY_ACQUIRE( ... )
#endif

#if __has_attribute( try_acquire_shared_capability )
#  define NOVA_SYNC_TRY_ACQUIRE_SHARED( ... ) __attribute__( ( try_acquire_shared_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_TRY_ACQUIRE_SHARED( ... )
#endif

#if __has_attribute( requires_capability )
#  define NOVA_SYNC_REQUIRES( ... ) __attribute__( ( requires_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_REQUIRES( ... )
#endif

#if __has_attribute( requires_shared_capability )
#  define NOVA_SYNC_REQUIRES_SHARED( ... ) __attribute__( ( requires_shared_capability( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_REQUIRES_SHARED( ... )
#endif

#if __has_attribute( locks_excluded )
#  define NOVA_SYNC_EXCLUDES( ... ) __attribute__( ( locks_excluded( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_EXCLUDES( ... )
#endif

#if __has_attribute( guarded_by )
#  define NOVA_SYNC_GUARDED_BY( ... ) __attribute__( ( guarded_by( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_GUARDED_BY( ... )
#endif

#if __has_attribute( pt_guarded_by )
#  define NOVA_SYNC_PT_GUARDED_BY( ... ) __attribute__( ( pt_guarded_by( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_PT_GUARDED_BY( ... )
#endif

#if __has_attribute( acquired_before )
#  define NOVA_SYNC_ACQUIRED_BEFORE( ... ) __attribute__( ( acquired_before( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_ACQUIRED_BEFORE( ... )
#endif

#if __has_attribute( acquired_after )
#  define NOVA_SYNC_ACQUIRED_AFTER( ... ) __attribute__( ( acquired_after( __VA_ARGS__ ) ) )
#else
#  define NOVA_SYNC_ACQUIRED_AFTER( ... )
#endif

#if __has_attribute( assert_capability )
#  define NOVA_SYNC_ASSERT_CAPABILITY( x ) __attribute__( ( assert_capability( x ) ) )
#else
#  define NOVA_SYNC_ASSERT_CAPABILITY( x )
#endif

#if __has_attribute( assert_shared_capability )
#  define NOVA_SYNC_ASSERT_SHARED_CAPABILITY( x ) __attribute__( ( assert_shared_capability( x ) ) )
#else
#  define NOVA_SYNC_ASSERT_SHARED_CAPABILITY( x )
#endif


#if __has_attribute( no_thread_safety_analysis )
#  define NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS __attribute__( ( no_thread_safety_analysis ) )
#else
#  define NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
#endif
