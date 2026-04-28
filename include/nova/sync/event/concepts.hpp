// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <concepts>
#include <utility>

namespace nova::sync::concepts {

// auto_reset_event: signal(), wait(), try_wait() -> bool
template < typename E >
concept auto_reset_event = requires( E& e ) {
    { e.signal() };
    { e.wait() };
    { e.try_wait() } -> std::convertible_to< bool >;
};

// manual_reset_event: signal(), reset(), wait(), try_wait() -> bool
template < typename E >
concept manual_reset_event = requires( E& e ) {
    { e.signal() };
    { e.reset() };
    { e.wait() };
    { e.try_wait() } -> std::convertible_to< bool >;
};

// timed_event: supports wait_for(duration) and wait_until(time_point)
template < typename E >
concept timed_event = requires( E& e ) {
    { e.try_wait_for( std::chrono::milliseconds {} ) } -> std::convertible_to< bool >;
    { e.try_wait_until( std::chrono::steady_clock::time_point {} ) } -> std::convertible_to< bool >;
    { e.try_wait_until( std::chrono::system_clock::time_point {} ) } -> std::convertible_to< bool >;
};

// native_async_event: exposes native_handle_type and native_handle() const
template < typename E >
concept native_async_event = requires( const E& e ) {
    typename E::native_handle_type;
    { std::as_const( e ).native_handle() } -> std::same_as< typename E::native_handle_type >;
};

} // namespace nova::sync::concepts
