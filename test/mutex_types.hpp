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
#include <nova/sync/mutex/win32_mutex.hpp>
#include <nova/sync/mutex/win32_recursive_mutex.hpp>
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

#ifdef NOVA_SYNC_HAS_WIN32_RECURSIVE_MUTEX
#    define NOVA_SYNC_WIN32_RECURSIVE_MUTEX_arg , nova::sync::win32_recursive_mutex
#else
#    define NOVA_SYNC_WIN32_RECURSIVE_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_WIN32_MUTEX
#    define NOVA_SYNC_WIN32_MUTEX_arg , nova::sync::win32_mutex
#else
#    define NOVA_SYNC_WIN32_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_WIN32_SRW_MUTEX
#    define NOVA_SYNC_WIN32_SRW_MUTEX_arg , nova::sync::win32_srw_mutex
#else
#    define NOVA_SYNC_WIN32_SRW_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_APPLE_OS_UNFAIR_MUTEX
#    define NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg , nova::sync::apple_os_unfair_mutex
#else
#    define NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX
#    define NOVA_SYNC_KQUEUE_MUTEX_arg , nova::sync::kqueue_mutex
#else
#    define NOVA_SYNC_KQUEUE_MUTEX_arg
#endif

#ifdef NOVA_SYNC_HAS_EVENTFD_MUTEX
#    define NOVA_SYNC_EVENTFD_MUTEX_arg , nova::sync::eventfd_mutex
#else
#    define NOVA_SYNC_EVENTFD_MUTEX_arg
#endif

// clang-format off
#define NOVA_SYNC_MUTEX_TEST_EXTRA_TYPES \
NOVA_SYNC_HAS_PTHREAD_SPINLOCK_arg   \
    NOVA_SYNC_HAS_PTHREAD_RT_MUTEX_arg   \
    NOVA_SYNC_WIN32_RECURSIVE_MUTEX_arg  \
    NOVA_SYNC_WIN32_MUTEX_arg            \
    NOVA_SYNC_WIN32_SRW_MUTEX_arg        \
    NOVA_SYNC_APPLE_OS_UNFAIR_MUTEX_arg  \
    NOVA_SYNC_KQUEUE_MUTEX_arg           \
    NOVA_SYNC_EVENTFD_MUTEX_arg
// clang-format on
