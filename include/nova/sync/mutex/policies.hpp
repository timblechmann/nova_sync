// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/parameter/parameter.hpp>

namespace nova::sync {

//----------------------------------------------------------------------------------------------------------------------
// Backoff policy tags

namespace detail {

struct exponential_backoff_tag
{};
struct recursive_tag
{};
struct timed_tag
{};
struct async_capable_tag
{};
struct shared_tag
{};
struct fair_tag
{};

} // namespace detail

//----------------------------------------------------------------------------------------------------------------------
// Backoff policies

/// @brief Policy: exponential backoff before parking.
using with_backoff = parameter::flag_param< detail::exponential_backoff_tag >;

/// @brief Policy: use futex-based timed wait in parking mutex.
using timed = parameter::flag_param< detail::timed_tag >;

//----------------------------------------------------------------------------------------------------------------------
// select_mutex trait policies

/// @brief Trait: select a mutex that supports recursive locking.
using recursive = parameter::flag_param< detail::recursive_tag >;

/// @brief Trait: select a mutex that supports timed waits (try_lock_for / try_lock_until).
using timed = parameter::flag_param< detail::timed_tag >;

/// @brief Trait: select a mutex that supports async acquisition (native_async_mutex protocol).
using async_capable = parameter::flag_param< detail::async_capable_tag >;

/// @brief Trait: select a mutex that supports shared (reader-writer) locking.
using shared = parameter::flag_param< detail::shared_tag >;

/// @brief Trait: select a ticket/fair mutex instead of a parking mutex.
using fair = parameter::flag_param< detail::fair_tag >;

//----------------------------------------------------------------------------------------------------------------------
// Internal extraction helpers

namespace detail {

using backoff_allowed_tags = std::tuple< exponential_backoff_tag >;

using select_mutex_allowed_tags
    = std::tuple< recursive_tag, timed_tag, async_capable_tag, shared_tag, fair_tag, exponential_backoff_tag >;

template < typename... Policies >
inline constexpr bool has_backoff_v = parameter::has_parameter_v< exponential_backoff_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_timed_v = parameter::has_parameter_v< timed_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_recursive_v = parameter::has_parameter_v< recursive_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_async_capable_v = parameter::has_parameter_v< async_capable_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_shared_v = parameter::has_parameter_v< shared_tag, Policies... >;

template < typename... Policies >
inline constexpr bool has_fair_v = parameter::has_parameter_v< fair_tag, Policies... >;

} // namespace detail

} // namespace nova::sync
