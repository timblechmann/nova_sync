// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// Shared type list macros for async-event tests.
///
/// NOVA_SYNC_ASYNC_AUTO_EVENT_TYPES  — native_auto_reset_event on platforms that expose native_handle()
/// NOVA_SYNC_ASYNC_MANUAL_EVENT_TYPES — native_manual_reset_event (available on all platforms)
/// NOVA_SYNC_ASYNC_EVENT_TYPES       — both combined (for generic tests that work with either)

#include <nova/sync/event/native_auto_reset_event.hpp>
#include <nova/sync/event/native_manual_reset_event.hpp>

// native_manual_reset_event always exposes native_handle()
#define NOVA_SYNC_ASYNC_MANUAL_EVENT_TYPES nova::sync::native_manual_reset_event

// native_auto_reset_event only exposes native_handle() on Win32 and Apple
#if defined( _WIN32 ) || defined( __APPLE__ )
#  define NOVA_SYNC_ASYNC_AUTO_EVENT_TYPES nova::sync::native_auto_reset_event
#  define NOVA_SYNC_ASYNC_EVENT_TYPES      nova::sync::native_auto_reset_event, nova::sync::native_manual_reset_event
#else
// On Linux, native_auto_reset_event is not async-capable (no native_handle())
#  define NOVA_SYNC_ASYNC_AUTO_EVENT_TYPES
#  define NOVA_SYNC_ASYNC_EVENT_TYPES nova::sync::native_manual_reset_event
#endif
