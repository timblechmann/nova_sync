// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

/// @file event/detail/async_support.hpp
///
/// Event-specific async support helpers (wraps unified handler invocation).
/// Uses the generic utilities from nova/sync/detail/async_support.hpp.

#include <nova/sync/detail/async_support.hpp>
#include <nova/sync/event/concepts.hpp>

#if defined( NOVA_SYNC_HAS_EXPECTED )

#  include <system_error>

namespace nova::sync::detail {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template < typename Handler >
void invoke_event_success( Handler&& handler )
{
    invoke_void_handler_success( std::forward< Handler >( handler ) );
}

template < typename Handler >
void invoke_event_error( Handler&& handler, std::error_code ec )
{
    invoke_handler_error< void, Handler >( std::forward< Handler >( handler ), ec );
}

template < typename Handler >
void invoke_event_error( Handler&& handler, std::errc ec )
{
    invoke_event_error( std::forward< Handler >( handler ), std::make_error_code( ec ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief Concept: handler is callable with `expected<void, std::error_code>`.
template < typename Handler >
concept invocable_with_void_expected = false // Base case so we can cleanly chain || with macros
#  ifdef NOVA_SYNC_HAS_STD_EXPECTED
                                       || std::invocable< Handler, std::expected< void, std::error_code > >
#  endif
#  ifdef NOVA_SYNC_HAS_TL_EXPECTED
                                       || std::invocable< Handler, tl::expected< void, std::error_code > >
#  endif
    ;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace nova::sync::detail

#endif // defined( NOVA_SYNC_HAS_EXPECTED )
