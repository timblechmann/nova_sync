// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/concepts.hpp>

// Platform-specific async mutex implementations
#if defined( _WIN32 )
#  include <nova/sync/mutex/win32_event_mutex.hpp>
#elif defined( __APPLE__ )
#  include <nova/sync/mutex/kqueue_mutex.hpp>
#elif defined( __linux__ )
#  include <nova/sync/mutex/eventfd_mutex.hpp>
#endif

namespace nova::sync {

namespace concepts {

} // namespace concepts

#if defined( _WIN32 )

using native_fast_async_mutex = win32_event_mutex;
using native_async_mutex      = win32_event_mutex;

#elif defined( __APPLE__ )

using native_fast_async_mutex = kqueue_mutex< with_backoff >;
using native_async_mutex      = kqueue_mutex<>;

#elif defined( __linux__ )

using native_fast_async_mutex = eventfd_mutex< with_backoff >;
using native_async_mutex      = eventfd_mutex<>;

#endif

// Validate the concepts at compile time for the current platform's aliases.
#if defined( _WIN32 ) || defined( __APPLE__ ) || defined( __linux__ )
static_assert( nova::sync::concepts::native_async_mutex< native_fast_async_mutex >,
               "native_async_mutex does not satisfy the native_async_mutex concept" );
static_assert( nova::sync::concepts::native_async_mutex< native_async_mutex >,
               "native_async_mutex does not satisfy the native_async_mutex concept" );
#endif

#if defined( __APPLE__ ) || defined( __linux__ ) || defined( _WIN32 )
static_assert( nova::sync::concepts::async_waiter_mutex< native_fast_async_mutex >,
               "native_fast_async_mutex does not satisfy the async_waiter_mutex concept" );
#endif

} // namespace nova::sync
