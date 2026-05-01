// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

/// @file policy_mutex_test.cpp
/// @brief Tests for policy-based mutex types and select_mutex.

#include <catch2/catch_all.hpp>

#include <nova/sync/event/parking_auto_reset_event.hpp>
#include <nova/sync/event/parking_manual_reset_event.hpp>
#include <nova/sync/mutex/parking_mutex.hpp>
#include <nova/sync/mutex/policies.hpp>
#include <nova/sync/mutex/spinlock_mutex.hpp>
#include <nova/sync/mutex/ticket_mutex.hpp>
#include <nova/sync/semaphore/parking_semaphore.hpp>

#include <nova/sync/event/concepts.hpp>
#include <nova/sync/mutex/concepts.hpp>
#include <nova/sync/semaphore/concepts.hpp>

//----------------------------------------------------------------------------------------------------------------------
// Concept checks: parking_mutex

static_assert( nova::sync::concepts::timed_mutex< nova::sync::parking_mutex< nova::sync::timed > > );
static_assert(
    nova::sync::concepts::timed_mutex< nova::sync::parking_mutex< nova::sync::timed, nova::sync::with_backoff > > );

//----------------------------------------------------------------------------------------------------------------------
// Concept checks: ticket_mutex

static_assert( nova::sync::concepts::timed_mutex< nova::sync::ticket_mutex<> > );
static_assert( nova::sync::concepts::timed_mutex< nova::sync::ticket_mutex< nova::sync::with_backoff > > );

//----------------------------------------------------------------------------------------------------------------------
// Concept checks: spinlock variants

static_assert( nova::sync::concepts::mutex< nova::sync::spinlock_mutex<> > );
static_assert( nova::sync::concepts::mutex< nova::sync::spinlock_mutex< nova::sync::with_backoff > > );

static_assert( nova::sync::concepts::recursive_mutex< nova::sync::recursive_spinlock_mutex<> > );
static_assert( nova::sync::concepts::recursive_mutex< nova::sync::recursive_spinlock_mutex< nova::sync::with_backoff > > );

static_assert( nova::sync::concepts::shared_mutex< nova::sync::shared_spinlock_mutex<> > );

//----------------------------------------------------------------------------------------------------------------------
// Concept checks: semaphores

static_assert( nova::sync::concepts::counting_semaphore< nova::sync::parking_semaphore<> > );
static_assert( nova::sync::concepts::counting_semaphore< nova::sync::parking_semaphore< nova::sync::with_backoff > > );
static_assert( nova::sync::concepts::timed_counting_semaphore< nova::sync::timed_semaphore<> > );
static_assert( nova::sync::concepts::timed_counting_semaphore< nova::sync::timed_semaphore< nova::sync::with_backoff > > );

// Aliases
static_assert( std::is_same_v< nova::sync::fast_semaphore, nova::sync::parking_semaphore<> > );
static_assert( std::is_same_v< nova::sync::fast_timed_semaphore, nova::sync::timed_semaphore<> > );

//----------------------------------------------------------------------------------------------------------------------
// Concept checks: events

static_assert( nova::sync::concepts::auto_reset_event< nova::sync::parking_auto_reset_event<> > );
static_assert(
    nova::sync::concepts::auto_reset_event< nova::sync::parking_auto_reset_event< nova::sync::with_backoff > > );
static_assert( nova::sync::concepts::auto_reset_event< nova::sync::auto_reset_event > );

static_assert( nova::sync::concepts::manual_reset_event< nova::sync::parking_manual_reset_event<> > );
static_assert(
    nova::sync::concepts::manual_reset_event< nova::sync::parking_manual_reset_event< nova::sync::with_backoff > > );
static_assert( nova::sync::concepts::manual_reset_event< nova::sync::manual_reset_event > );
