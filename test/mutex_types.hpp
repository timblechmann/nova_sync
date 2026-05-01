#pragma once

#include <nova/sync/mutex/apple_os_unfair_mutex.hpp>
#include <nova/sync/mutex/eventfd_mutex.hpp>
#include <nova/sync/mutex/kqueue_mutex.hpp>
#include <nova/sync/mutex/parking_mutex.hpp>
#include <nova/sync/mutex/pthread_mutex.hpp>
#include <nova/sync/mutex/pthread_spinlock_mutex.hpp>
#include <nova/sync/mutex/spinlock_mutex.hpp>
#include <nova/sync/mutex/ticket_mutex.hpp>
#include <nova/sync/mutex/win32_critical_section_mutex.hpp>
#include <nova/sync/mutex/win32_event_mutex.hpp>
#include <nova/sync/mutex/win32_mutex.hpp>
#include <nova/sync/mutex/win32_srw_mutex.hpp>

#ifdef NOVA_SYNC_HAS_PTHREAD_SPINLOCK
#  define NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg , nova::sync::pthread_spinlock_mutex
#else
#  define NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg
#endif

#ifdef NOVA_SYNC_HAS_PTHREAD_MUTEX
#  define NOVA_SYNC_PTHREAD_NONRECURSIVE_MUTEX_arg                                                                    \
      , nova::sync::pthread_default_mutex, nova::sync::pthread_mutex< nova::sync::pthread_policy::pthread_adaptive >, \
          nova::sync::pthread_mutex< nova::sync::pthread_policy::pthread_errorcheck >
#  define NOVA_SYNC_PTHREAD_RECURSIVE_MUTEX_arg , nova::sync::pthread_mutex< nova::sync::recursive >
#else
#  define NOVA_SYNC_PTHREAD_NONRECURSIVE_MUTEX_arg
#  define NOVA_SYNC_PTHREAD_RECURSIVE_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_PTHREAD_RT_MUTEX
#  define NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg , nova::sync::pthread_priority_inherit_mutex
#else
#  define NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg
#endif

#ifdef _WIN32
#  define NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg , nova::sync::win32_critical_section_mutex<>
#  define NOVA_SYNC_WIN32_EVENT_MUTEX_arg            , nova::sync::win32_event_mutex
#  define NOVA_SYNC_WIN32_MUTEX_arg                  , nova::sync::win32_mutex
#  define NOVA_SYNC_WIN32_SRW_MUTEX_arg              , nova::sync::win32_srw_mutex
#  define NOVA_SYNC_ASYNC_MUTEX_TYPES                nova::sync::win32_event_mutex
#else
#  define NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg
#  define NOVA_SYNC_WIN32_EVENT_MUTEX_arg
#  define NOVA_SYNC_WIN32_MUTEX_arg
#  define NOVA_SYNC_WIN32_SRW_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX
#  define NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg , nova::sync::apple_os_unfair_mutex
#else
#  define NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX
#  define NOVA_SYNC_KQUEUE_MUTEX_arg      , nova::sync::kqueue_mutex<>
#  define NOVA_SYNC_FAST_KQUEUE_MUTEX_arg , nova::sync::kqueue_mutex< nova::sync::with_backoff >
#  define NOVA_SYNC_ASYNC_MUTEX_TYPES nova::sync::kqueue_mutex<>, nova::sync::kqueue_mutex< nova::sync::with_backoff >
#else
#  define NOVA_SYNC_KQUEUE_MUTEX_arg
#  define NOVA_SYNC_FAST_KQUEUE_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX
#  define NOVA_SYNC_EVENTFD_MUTEX_arg      , nova::sync::eventfd_mutex<>
#  define NOVA_SYNC_FAST_EVENTFD_MUTEX_arg , nova::sync::eventfd_mutex< nova::sync::with_backoff >
#  define NOVA_SYNC_ASYNC_MUTEX_TYPES nova::sync::eventfd_mutex<>, nova::sync::eventfd_mutex< nova::sync::with_backoff >
#else
#  define NOVA_SYNC_EVENTFD_MUTEX_arg
#  define NOVA_SYNC_FAST_EVENTFD_MUTEX_arg
#endif

// clang-format off

#define NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES       \
    NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg         \
    NOVA_SYNC_PTHREAD_RECURSIVE_MUTEX_arg      \
    NOVA_SYNC_PTHREAD_NONRECURSIVE_MUTEX_arg   \
    NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg         \
    NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg \
    NOVA_SYNC_WIN32_MUTEX_arg                  \
    NOVA_SYNC_WIN32_EVENT_MUTEX_arg            \
    NOVA_SYNC_WIN32_SRW_MUTEX_arg              \
    NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg        \
    NOVA_SYNC_KQUEUE_MUTEX_arg                 \
    NOVA_SYNC_FAST_KQUEUE_MUTEX_arg            \
    NOVA_SYNC_EVENTFD_MUTEX_arg                \
    NOVA_SYNC_FAST_EVENTFD_MUTEX_arg

/// @brief All mutex types available on this platform.
/// First type has no leading comma; the rest come via _arg macros that supply their own.
/// Use after explicit types in TEMPLATE_TEST_CASE or as the sole type-list.

// clang-format off

using parking_timed_mutex_with_backoff      = nova::sync::parking_mutex< nova::sync::timed, nova::sync::with_backoff >;
using spinlock_mutex_with_backoff           = nova::sync::spinlock_mutex< nova::sync::with_backoff >;
using spinlock_mutex_recursive              = nova::sync::spinlock_mutex< nova::sync::recursive >;
using spinlock_mutex_recursive_with_backoff = nova::sync::spinlock_mutex< nova::sync::recursive, nova::sync::with_backoff >;
using spinlock_mutex_shared                 = nova::sync::spinlock_mutex< nova::sync::shared >;
using spinlock_mutex_shared_with_backoff    = nova::sync::spinlock_mutex< nova::sync::shared, nova::sync::with_backoff >;

#define NOVA_SYNC_ALL_MUTEX_TYPES                        \
    nova::sync::parking_mutex<>,                         \
    nova::sync::parking_mutex<nova::sync::with_backoff>, \
    nova::sync::parking_mutex<nova::sync::timed>,        \
    parking_timed_mutex_with_backoff,                    \
    nova::sync::ticket_mutex<>,                          \
    nova::sync::ticket_mutex<nova::sync::with_backoff>,  \
    nova::sync::spinlock_mutex<>,                        \
    spinlock_mutex_with_backoff,                         \
    spinlock_mutex_recursive,                            \
    spinlock_mutex_recursive_with_backoff,               \
    spinlock_mutex_shared,                               \
    spinlock_mutex_shared_with_backoff                   \
    NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES

#define NOVA_SYNC_NON_RECURSIVE_MUTEX_TYPES          \
    nova::sync::parking_mutex<>,                     \
    nova::sync::parking_mutex<nova::sync::with_backoff>, \
    nova::sync::ticket_mutex<>,                      \
    nova::sync::ticket_mutex<nova::sync::with_backoff>,  \
    nova::sync::spinlock_mutex<>,                    \
    spinlock_mutex_shared                            \
    NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES

#define NOVA_SYNC_RECURSIVE_MUTEX_TYPES              \
    spinlock_mutex_recursive                         \
    NOVA_SYNC_PTHREAD_RECURSIVE_MUTEX_arg            \
    NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg

#define NOVA_SYNC_TIMED_MUTEX_TYPES                  \
    nova::sync::parking_mutex<nova::sync::timed>,    \
    nova::sync::ticket_mutex<>,                      \
    nova::sync::ticket_mutex<nova::sync::with_backoff>  \
    NOVA_SYNC_WIN32_EVENT_MUTEX_arg                  \
    NOVA_SYNC_KQUEUE_MUTEX_arg                       \
    NOVA_SYNC_FAST_KQUEUE_MUTEX_arg                  \
    NOVA_SYNC_EVENTFD_MUTEX_arg                      \
    NOVA_SYNC_FAST_EVENTFD_MUTEX_arg

#define NOVA_SYNC_SHARED_MUTEX_TYPES                 \
    spinlock_mutex_shared,                           \
    spinlock_mutex_shared_with_backoff

#define NOVA_SYNC_ASYNC_MUTEX_TEST_TYPES \
    NOVA_SYNC_WIN32_EVENT_MUTEX_arg      \
    NOVA_SYNC_KQUEUE_MUTEX_arg           \
    NOVA_SYNC_FAST_KQUEUE_MUTEX_arg      \
    NOVA_SYNC_EVENTFD_MUTEX_arg          \
    NOVA_SYNC_FAST_EVENTFD_MUTEX_arg

// clang-format on
