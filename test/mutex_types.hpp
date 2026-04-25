#pragma once

#include <nova/sync/mutex/apple_os_unfair_mutex.hpp>
#include <nova/sync/mutex/eventfd_mutex.hpp>
#include <nova/sync/mutex/fair_mutex.hpp>
#include <nova/sync/mutex/fast_mutex.hpp>
#include <nova/sync/mutex/kqueue_mutex.hpp>
#include <nova/sync/mutex/pthread_rt_mutex.hpp>
#include <nova/sync/mutex/pthread_spinlock_mutex.hpp>
#include <nova/sync/mutex/recursive_spinlock_mutex.hpp>
#include <nova/sync/mutex/shared_spinlock_mutex.hpp>
#include <nova/sync/mutex/spinlock_mutex.hpp>
#include <nova/sync/mutex/win32_critical_section_mutex.hpp>
#include <nova/sync/mutex/win32_event_mutex.hpp>
#include <nova/sync/mutex/win32_mutex.hpp>
#include <nova/sync/mutex/win32_srw_mutex.hpp>

#ifdef NOVA_SYNC_HAS_PTHREAD_SPINLOCK
#    define NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg , nova::sync::pthread_spinlock_mutex
#else
#    define NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg
#endif

#ifdef NOVA_SYNC_HAS_PTHREAD_RT_MUTEX
#    define NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg , nova::sync::pthread_priority_inherit_mutex
#else
#    define NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg
#endif

#ifdef _WIN32
#    define NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg , nova::sync::win32_critical_section_mutex
#    define NOVA_SYNC_WIN32_EVENT_MUTEX_arg            , nova::sync::win32_event_mutex
#    define NOVA_SYNC_WIN32_MUTEX_arg                  , nova::sync::win32_mutex
#    define NOVA_SYNC_WIN32_SRW_MUTEX_arg              , nova::sync::win32_srw_mutex
#    define NOVA_SYNC_ASYNC_MUTEX_TYPES                nova::sync::win32_event_mutex
#else
#    define NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg
#    define NOVA_SYNC_WIN32_EVENT_MUTEX_arg
#    define NOVA_SYNC_WIN32_MUTEX_arg
#    define NOVA_SYNC_WIN32_SRW_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX
#else
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX
#else
#endif

#ifdef NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX
#    define NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg , nova::sync::apple_os_unfair_mutex
#else
#    define NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX
#    define NOVA_SYNC_KQUEUE_MUTEX_arg      , nova::sync::kqueue_mutex
#    define NOVA_SYNC_FAST_KQUEUE_MUTEX_arg , nova::sync::fast_kqueue_mutex
#    define NOVA_SYNC_ASYNC_MUTEX_TYPES     nova::sync::kqueue_mutex, nova::sync::fast_kqueue_mutex
#else
#    define NOVA_SYNC_KQUEUE_MUTEX_arg
#    define NOVA_SYNC_FAST_KQUEUE_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX
#    define NOVA_SYNC_EVENTFD_MUTEX_arg      , nova::sync::eventfd_mutex
#    define NOVA_SYNC_FAST_EVENTFD_MUTEX_arg , nova::sync::fast_eventfd_mutex
#    define NOVA_SYNC_ASYNC_MUTEX_TYPES      nova::sync::eventfd_mutex, nova::sync::fast_eventfd_mutex
#else
#    define NOVA_SYNC_EVENTFD_MUTEX_arg
#    define NOVA_SYNC_FAST_EVENTFD_MUTEX_arg
#endif

// clang-format off
#define NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES        \
NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg              \
    NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg          \
    NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg  \
    NOVA_SYNC_WIN32_MUTEX_arg                   \
    NOVA_SYNC_WIN32_EVENT_MUTEX_arg             \
    NOVA_SYNC_WIN32_SRW_MUTEX_arg               \
    NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg         \
    NOVA_SYNC_KQUEUE_MUTEX_arg                  \
    NOVA_SYNC_FAST_KQUEUE_MUTEX_arg             \
    NOVA_SYNC_EVENTFD_MUTEX_arg                 \
    NOVA_SYNC_FAST_EVENTFD_MUTEX_arg
// clang-format on

/// @brief All mutex types available on this platform.
/// First type has no leading comma; the rest come via _arg macros that supply their own.
/// Use after explicit types in TEMPLATE_TEST_CASE or as the sole type-list.
// clang-format off
#define NOVA_SYNC_ALL_MUTEX_TYPES         \
    nova::sync::fast_mutex,               \
    nova::sync::fair_mutex,               \
    nova::sync::spinlock_mutex,           \
    nova::sync::recursive_spinlock_mutex, \
    nova::sync::shared_spinlock_mutex     \
    NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES
// clang-format on

/// @brief All non-recursive mutex types available on this platform.
/// These satisfy `concepts::mutex` but NOT `concepts::recursive_mutex`.
// clang-format off
#define NOVA_SYNC_NON_RECURSIVE_MUTEX_TYPES     \
    nova::sync::fast_mutex,                     \
    nova::sync::fair_mutex,                     \
    nova::sync::spinlock_mutex,                 \
    nova::sync::shared_spinlock_mutex           \
    NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg          \
    NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg          \
    NOVA_SYNC_WIN32_EVENT_MUTEX_arg             \
    NOVA_SYNC_WIN32_MUTEX_arg                   \
    NOVA_SYNC_WIN32_SRW_MUTEX_arg               \
    NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg         \
    NOVA_SYNC_KQUEUE_MUTEX_arg                  \
    NOVA_SYNC_FAST_KQUEUE_MUTEX_arg             \
    NOVA_SYNC_EVENTFD_MUTEX_arg                 \
    NOVA_SYNC_FAST_EVENTFD_MUTEX_arg
// clang-format on

/// @brief All recursive mutex types available on this platform.
/// These satisfy `concepts::recursive_mutex`.
// clang-format off
#define NOVA_SYNC_RECURSIVE_MUTEX_TYPES         \
    nova::sync::recursive_spinlock_mutex        \
    NOVA_SYNC_WIN32_CRITICAL_SECTION_MUTEX_arg
// clang-format on

/// @brief All timed mutex types available on this platform.
/// These satisfy `concepts::timed_mutex` (have try_lock_for / try_lock_until).
/// The first type in each platform branch has no leading comma so this macro
/// can be used as the sole type list in TEMPLATE_TEST_CASE.
// clang-format off
#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX
#    define NOVA_SYNC_TIMED_MUTEX_TYPES             \
        nova::sync::kqueue_mutex,                   \
        nova::sync::fast_kqueue_mutex               \
        NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg
#elif defined( NOVA_SYNC_HAS_EVENTFD_MUTEX )
#    define NOVA_SYNC_TIMED_MUTEX_TYPES             \
        nova::sync::eventfd_mutex,                  \
        nova::sync::fast_eventfd_mutex              \
        NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg
#elif defined( _WIN32 )
#    define NOVA_SYNC_TIMED_MUTEX_TYPES             \
        nova::sync::win32_event_mutex,              \
        nova::sync::win32_mutex
#elif defined( NOVA_SYNC_HAS_PTHREAD_RT_MUTEX )
#    define NOVA_SYNC_TIMED_MUTEX_TYPES             \
        nova::sync::pthread_priority_inherit_mutex
#endif
// clang-format on

/// @brief All shared mutex types available on this platform.
/// These satisfy `concepts::shared_mutex` (have lock_shared / unlock_shared / try_lock_shared).
// clang-format off
#define NOVA_SYNC_SHARED_MUTEX_TYPES            \
    nova::sync::shared_spinlock_mutex
// clang-format on

/// @brief Macro listing only the async-capable mutex types available on this platform.
///
/// Used in async-specific TEMPLATE_TEST_CASEs to run tests for all mutex types
/// that support async acquisition via the event-loop integration layer.
// clang-format off
#define NOVA_SYNC_ASYNC_MUTEX_TEST_TYPES \
    NOVA_SYNC_WIN32_EVENT_MUTEX_arg      \
    NOVA_SYNC_KQUEUE_MUTEX_arg           \
    NOVA_SYNC_FAST_KQUEUE_MUTEX_arg      \
    NOVA_SYNC_EVENTFD_MUTEX_arg          \
    NOVA_SYNC_FAST_EVENTFD_MUTEX_arg
// clang-format on
