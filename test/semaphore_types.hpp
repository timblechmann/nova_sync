// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// Shared type list macros for semaphore tests.

#include <nova/sync/semaphore/dispatch_semaphore.hpp>
#include <nova/sync/semaphore/eventfd_semaphore.hpp>
#include <nova/sync/semaphore/kqueue_semaphore.hpp>
#include <nova/sync/semaphore/mach_semaphore.hpp>
#include <nova/sync/semaphore/native_async_semaphore.hpp>
#include <nova/sync/semaphore/parking_semaphore.hpp>
#include <nova/sync/semaphore/posix_semaphore.hpp>
#include <nova/sync/semaphore/win32_semaphore.hpp>

// ---------------------------------------------------------------------------
// Platform-conditional type args (each starts with a comma)
// ---------------------------------------------------------------------------

#ifdef NOVA_SYNC_HAS_EVENTFD_SEMAPHORE
#  define NOVA_SYNC_EVENTFD_SEMAPHORE_arg , nova::sync::eventfd_semaphore
#else
#  define NOVA_SYNC_EVENTFD_SEMAPHORE_arg
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_SEMAPHORE
#  define NOVA_SYNC_KQUEUE_SEMAPHORE_arg , nova::sync::kqueue_semaphore
#else
#  define NOVA_SYNC_KQUEUE_SEMAPHORE_arg
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SEMAPHORE
#  define NOVA_SYNC_WIN32_SEMAPHORE_arg , nova::sync::win32_semaphore
#else
#  define NOVA_SYNC_WIN32_SEMAPHORE_arg
#endif

#ifdef NOVA_SYNC_HAS_POSIX_SEMAPHORE
#  define NOVA_SYNC_POSIX_SEMAPHORE_arg , nova::sync::posix_semaphore
#else
#  define NOVA_SYNC_POSIX_SEMAPHORE_arg
#endif

#ifdef NOVA_SYNC_HAS_MACH_SEMAPHORE
#  define NOVA_SYNC_MACH_SEMAPHORE_arg , nova::sync::mach_semaphore
#else
#  define NOVA_SYNC_MACH_SEMAPHORE_arg
#endif

#ifdef NOVA_SYNC_HAS_DISPATCH_SEMAPHORE
#  define NOVA_SYNC_DISPATCH_SEMAPHORE_arg , nova::sync::dispatch_semaphore
#else
#  define NOVA_SYNC_DISPATCH_SEMAPHORE_arg
#endif

// ---------------------------------------------------------------------------
// All semaphore types
// ---------------------------------------------------------------------------

// clang-format off
#define NOVA_SYNC_ALL_SEMAPHORE_TYPES                \
    nova::sync::fast_semaphore,                      \
    nova::sync::fast_timed_semaphore                 \
    NOVA_SYNC_EVENTFD_SEMAPHORE_arg                  \
    NOVA_SYNC_KQUEUE_SEMAPHORE_arg                   \
    NOVA_SYNC_WIN32_SEMAPHORE_arg                    \
    NOVA_SYNC_POSIX_SEMAPHORE_arg                    \
    NOVA_SYNC_MACH_SEMAPHORE_arg                     \
    NOVA_SYNC_DISPATCH_SEMAPHORE_arg
// clang-format on

// ---------------------------------------------------------------------------
// Timed semaphore types (have try_acquire_for / try_acquire_until)
// ---------------------------------------------------------------------------

// clang-format off
#define NOVA_SYNC_TIMED_SEMAPHORE_TYPES              \
    nova::sync::fast_timed_semaphore                 \
    NOVA_SYNC_EVENTFD_SEMAPHORE_arg                  \
    NOVA_SYNC_KQUEUE_SEMAPHORE_arg                   \
    NOVA_SYNC_WIN32_SEMAPHORE_arg                    \
    NOVA_SYNC_POSIX_SEMAPHORE_arg                    \
    NOVA_SYNC_MACH_SEMAPHORE_arg                     \
    NOVA_SYNC_DISPATCH_SEMAPHORE_arg
// clang-format on

// ---------------------------------------------------------------------------
// Async-capable semaphore types (have native_handle())
// ---------------------------------------------------------------------------

// clang-format off
#ifdef NOVA_SYNC_HAS_EVENTFD_SEMAPHORE
#  define NOVA_SYNC_ASYNC_SEMAPHORE_TYPES nova::sync::eventfd_semaphore
#elif defined( NOVA_SYNC_HAS_KQUEUE_SEMAPHORE )
#  define NOVA_SYNC_ASYNC_SEMAPHORE_TYPES nova::sync::kqueue_semaphore
#elif defined( NOVA_SYNC_HAS_WIN32_SEMAPHORE )
#  define NOVA_SYNC_ASYNC_SEMAPHORE_TYPES nova::sync::win32_semaphore
#endif
// clang-format on
