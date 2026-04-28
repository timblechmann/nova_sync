// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/semaphore/concepts.hpp>

// Platform-specific async semaphore implementations
#if defined( _WIN32 )
#  include <nova/sync/semaphore/win32_semaphore.hpp>
#elif defined( __APPLE__ )
#  include <nova/sync/semaphore/kqueue_semaphore.hpp>
#elif defined( __linux__ )
#  include <nova/sync/semaphore/eventfd_semaphore.hpp>
#endif

namespace nova::sync {

#if defined( _WIN32 )

using native_async_semaphore = win32_semaphore;

#elif defined( __APPLE__ )

using native_async_semaphore = kqueue_semaphore;

#elif defined( __linux__ )

using native_async_semaphore = eventfd_semaphore;

#endif

// Validate the concepts at compile time for the current platform's alias.
#if defined( _WIN32 ) || defined( __APPLE__ ) || defined( __linux__ )
static_assert( nova::sync::concepts::native_async_semaphore< native_async_semaphore >,
               "native_async_semaphore does not satisfy the native_async_semaphore concept" );
#endif

} // namespace nova::sync
