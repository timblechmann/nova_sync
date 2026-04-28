// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <utility>

namespace nova::sync::concepts {

/// @brief Counting semaphore: `acquire()`, `release(n)`, `try_acquire()`.
template < typename S >
concept counting_semaphore = requires( S& s, std::ptrdiff_t n ) {
    { s.acquire() };
    { s.release() };
    { s.release( n ) };
    { s.try_acquire() } -> std::convertible_to< bool >;
};

/// @brief Counting semaphore with timed-wait operations.
template < typename S >
concept timed_counting_semaphore = counting_semaphore< S > && requires( S& s ) {
    { s.try_acquire_for( std::chrono::milliseconds {} ) } -> std::convertible_to< bool >;
    { s.try_acquire_until( std::chrono::steady_clock::time_point {} ) } -> std::convertible_to< bool >;
};

/// @brief Counting semaphore that exposes a native OS handle for async integration.
template < typename S >
concept native_async_semaphore = counting_semaphore< S > && requires( const S& s ) {
    typename S::native_handle_type;
    { std::as_const( s ).native_handle() } -> std::same_as< typename S::native_handle_type >;
};

} // namespace nova::sync::concepts
