// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file detail/async_support.hpp
///
/// Shared utilities for async handler invocation with expected<T, error_code>.
/// Used by both mutex and event async support implementations.

#if __cplusplus >= 202302L && __has_include( <expected> )
#  ifndef NOVA_SYNC_HAS_EXPECTED
#    define NOVA_SYNC_HAS_EXPECTED
#  endif
#  ifndef NOVA_SYNC_HAS_STD_EXPECTED
#    define NOVA_SYNC_HAS_STD_EXPECTED
#  endif
#  include <expected>
#endif

#if __has_include( <tl/expected.hpp> )
#  ifndef NOVA_SYNC_HAS_EXPECTED
#    define NOVA_SYNC_HAS_EXPECTED
#  endif
#  ifndef NOVA_SYNC_HAS_TL_EXPECTED
#    define NOVA_SYNC_HAS_TL_EXPECTED
#  endif
#  include <tl/expected.hpp>
#endif

#if defined( NOVA_SYNC_HAS_EXPECTED )

#  include <cassert>
#  include <functional>
#  include <system_error>

namespace nova::sync::detail {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic handler invocation with expected<T, error_code>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Invoke a handler with a successful `expected<T, error_code>` result.
///
/// Generic version for non-void types. Dispatches to the appropriate expected
/// implementation (std::expected or tl::expected) based on compile-time availability.
///
/// @tparam T The value type (e.g., std::unique_lock<Mutex>)
/// @tparam Handler The handler callable type
/// @param handler The handler to invoke
/// @param value The value to wrap in expected<T, error_code>
template < typename T, typename Handler >
void invoke_handler_success( Handler&& handler, T value )
{
#  ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< T, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type { std::move( value ) } );
#  endif
#  ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< T, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type { std::move( value ) } );
#  endif

    assert( false && "Handler must be invocable with expected<T, error_code>" );
#  ifdef __cpp_lib_unreachable
    std::unreachable();
#  endif
}

/// @brief Invoke a handler with a successful `expected<void, error_code>` result.
///
/// Specialization for void: invoke with no value argument.
template < typename Handler >
void invoke_void_handler_success( Handler&& handler )
{
#  ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< void, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type {} );
#  endif
#  ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< void, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type {} );
#  endif

    assert( false && "Handler must be invocable with expected<void, error_code>" );
#  ifdef __cpp_lib_unreachable
    std::unreachable();
#  endif
}

/// @brief Invoke a handler with an error `expected<T, error_code>` result.
template < typename T, typename Handler >
void invoke_handler_error( Handler&& handler, std::error_code ec )
{
#  ifdef NOVA_SYNC_HAS_STD_EXPECTED
    using std_expected_type = std::expected< T, std::error_code >;
    if constexpr ( std::invocable< Handler, std_expected_type > )
        return std::invoke( handler, std_expected_type { std::unexpect, ec } );
#  endif
#  ifdef NOVA_SYNC_HAS_TL_EXPECTED
    using tl_expected_type = tl::expected< T, std::error_code >;
    if constexpr ( std::invocable< Handler, tl_expected_type > )
        return std::invoke( handler, tl_expected_type { tl::unexpect, ec } );
#  endif

    assert( false && "Handler must be invocable with expected<T, error_code>" );
#  ifdef __cpp_lib_unreachable
    std::unreachable();
#  endif
}

template < typename T, typename Handler >
void invoke_handler_error( Handler&& handler, std::errc ec )
{
    invoke_handler_error< T, Handler >( std::forward< Handler >( handler ), std::make_error_code( ec ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace nova::sync::detail

#endif // defined( NOVA_SYNC_HAS_EXPECTED )
