// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

// ---------------------------------------------------------------------------
// Thread-safety analysis annotations (Clang only).
//
// Based on https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
// All macros expand to nothing on non-Clang compilers.
//
// Usage summary:
//   NOVA_SYNC_CAPABILITY("mutex")         — on a class: marks it as a mutex type
//   NOVA_SYNC_REENTRANT_CAPABILITY        — on a class: marks it as re-entrant
//   NOVA_SYNC_SCOPED_CAPABILITY           — on a class: RAII scoped lock
//   NOVA_SYNC_ACQUIRE(...)                — on lock()    : acquires exclusive capability
//   NOVA_SYNC_ACQUIRE_SHARED(...)         — on lock_shared(): acquires shared capability
//   NOVA_SYNC_RELEASE(...)                — on unlock()  : releases exclusive capability
//   NOVA_SYNC_RELEASE_SHARED(...)         — on unlock_shared(): releases shared capability
//   NOVA_SYNC_RELEASE_GENERIC(...)        — on unlock()  : releases exclusive or shared
//   NOVA_SYNC_TRY_ACQUIRE(val, ...)       — on try_lock(): tries to acquire exclusively
//   NOVA_SYNC_TRY_ACQUIRE_SHARED(val, .)  — on try_lock_shared(): tries to acquire shared
//   NOVA_SYNC_REQUIRES(...)               — on a function: caller must hold exclusive
//   NOVA_SYNC_REQUIRES_SHARED(...)        — on a function: caller must hold shared
//   NOVA_SYNC_EXCLUDES(...)               — on a function: caller must NOT hold
//   NOVA_SYNC_GUARDED_BY(...)             — on a data member: guarded by given mutex
//   NOVA_SYNC_PT_GUARDED_BY(...)          — on a pointer member: pointed-to data guarded
//   NOVA_SYNC_ACQUIRED_BEFORE(...)        — lock ordering constraint
//   NOVA_SYNC_ACQUIRED_AFTER(...)         — lock ordering constraint
//   NOVA_SYNC_ASSERT_CAPABILITY(...)      — asserts the capability is held at runtime
//   NOVA_SYNC_ASSERT_SHARED_CAPABILITY(.) — asserts shared capability is held
//   NOVA_SYNC_RETURN_CAPABILITY(...)      — on getter: identifies returned capability
//   NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS   — on a function: opt out of analysis
// ---------------------------------------------------------------------------

#ifndef __has_attribute
#    define __has_attribute( x ) 0
#endif

#if __has_attribute( capability )
#    define NOVA_SYNC_CAPABILITY( x ) __attribute__( ( capability( x ) ) )
#else
#    define NOVA_SYNC_CAPABILITY( x )
#endif

#if __has_attribute( reentrant_capability )
#    define NOVA_SYNC_REENTRANT_CAPABILITY __attribute__( ( reentrant_capability ) )
#else
#    define NOVA_SYNC_REENTRANT_CAPABILITY
#endif

#if __has_attribute( scoped_lockable )
#    define NOVA_SYNC_SCOPED_CAPABILITY __attribute__( ( scoped_lockable ) )
#else
#    define NOVA_SYNC_SCOPED_CAPABILITY
#endif

#if __has_attribute( acquire_capability )
#    define NOVA_SYNC_ACQUIRE( ... ) __attribute__( ( acquire_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_ACQUIRE( ... )
#endif

#if __has_attribute( acquire_shared_capability )
#    define NOVA_SYNC_ACQUIRE_SHARED( ... ) __attribute__( ( acquire_shared_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_ACQUIRE_SHARED( ... )
#endif

#if __has_attribute( release_capability )
#    define NOVA_SYNC_RELEASE( ... ) __attribute__( ( release_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_RELEASE( ... )
#endif

#if __has_attribute( release_shared_capability )
#    define NOVA_SYNC_RELEASE_SHARED( ... ) __attribute__( ( release_shared_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_RELEASE_SHARED( ... )
#endif

#if __has_attribute( release_generic_capability )
#    define NOVA_SYNC_RELEASE_GENERIC( ... ) __attribute__( ( release_generic_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_RELEASE_GENERIC( ... )
#endif

#if __has_attribute( try_acquire_capability )
#    define NOVA_SYNC_TRY_ACQUIRE( ... ) __attribute__( ( try_acquire_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_TRY_ACQUIRE( ... )
#endif

#if __has_attribute( try_acquire_shared_capability )
#    define NOVA_SYNC_TRY_ACQUIRE_SHARED( ... ) __attribute__( ( try_acquire_shared_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_TRY_ACQUIRE_SHARED( ... )
#endif

#if __has_attribute( requires_capability )
#    define NOVA_SYNC_REQUIRES( ... ) __attribute__( ( requires_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_REQUIRES( ... )
#endif

#if __has_attribute( requires_shared_capability )
#    define NOVA_SYNC_REQUIRES_SHARED( ... ) __attribute__( ( requires_shared_capability( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_REQUIRES_SHARED( ... )
#endif

#if __has_attribute( locks_excluded )
#    define NOVA_SYNC_EXCLUDES( ... ) __attribute__( ( locks_excluded( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_EXCLUDES( ... )
#endif

#if __has_attribute( guarded_by )
#    define NOVA_SYNC_GUARDED_BY( ... ) __attribute__( ( guarded_by( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_GUARDED_BY( ... )
#endif

#if __has_attribute( pt_guarded_by )
#    define NOVA_SYNC_PT_GUARDED_BY( ... ) __attribute__( ( pt_guarded_by( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_PT_GUARDED_BY( ... )
#endif

#if __has_attribute( acquired_before )
#    define NOVA_SYNC_ACQUIRED_BEFORE( ... ) __attribute__( ( acquired_before( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_ACQUIRED_BEFORE( ... )
#endif

#if __has_attribute( acquired_after )
#    define NOVA_SYNC_ACQUIRED_AFTER( ... ) __attribute__( ( acquired_after( __VA_ARGS__ ) ) )
#else
#    define NOVA_SYNC_ACQUIRED_AFTER( ... )
#endif

#if __has_attribute( assert_capability )
#    define NOVA_SYNC_ASSERT_CAPABILITY( x ) __attribute__( ( assert_capability( x ) ) )
#else
#    define NOVA_SYNC_ASSERT_CAPABILITY( x )
#endif

#if __has_attribute( assert_shared_capability )
#    define NOVA_SYNC_ASSERT_SHARED_CAPABILITY( x ) __attribute__( ( assert_shared_capability( x ) ) )
#else
#    define NOVA_SYNC_ASSERT_SHARED_CAPABILITY( x )
#endif

#if __has_attribute( lock_returned )
#    define NOVA_SYNC_RETURN_CAPABILITY( x ) __attribute__( ( lock_returned( x ) ) )
#else
#    define NOVA_SYNC_RETURN_CAPABILITY( x )
#endif

#if __has_attribute( no_thread_safety_analysis )
#    define NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS __attribute__( ( no_thread_safety_analysis ) )
#else
#    define NOVA_SYNC_NO_THREAD_SAFETY_ANALYSIS
#endif
