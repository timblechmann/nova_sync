// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/mutex/concepts.hpp>

// Platform-specific async mutex implementations
#if defined( _WIN32 )
#    include <nova/sync/mutex/win32_mutex.hpp>
#elif defined( __APPLE__ )
#    include <nova/sync/mutex/kqueue_mutex.hpp>
#elif defined( __linux__ )
#    include <nova/sync/mutex/eventfd_mutex.hpp>
#endif

namespace nova::sync {

namespace concepts {

} // namespace concepts

// ---------------------------------------------------------------------------
// Cross-platform type alias
// ---------------------------------------------------------------------------

#if defined( _WIN32 )

using native_async_mutex = win32_mutex;

#elif defined( __APPLE__ )

using native_async_mutex = kqueue_mutex;

#elif defined( __linux__ )

using native_async_mutex = eventfd_mutex;

#endif

// Validate the concept at compile time for the current platform's alias.
#if defined( _WIN32 ) || defined( __APPLE__ ) || defined( __linux__ )
static_assert( nova::sync::concepts::native_async_mutex< native_async_mutex >,
               "native_async_mutex does not satisfy the native_async_mutex concept" );
#endif

} // namespace nova::sync
