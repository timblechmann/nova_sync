// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if __cplusplus >= 202302L && __has_include( <expected> )
#    define NOVA_SYNC_HAS_EXPECTED
#    define NOVA_SYNC_HAS_STD_EXPECTED
#    include <expected>
#endif

#if __has_include( <tl/expected.hpp> )
#    ifndef NOVA_SYNC_HAS_EXPECTED
#        define NOVA_SYNC_HAS_EXPECTED
#    endif
#    define NOVA_SYNC_HAS_TL_EXPECTED
#    include <tl/expected.hpp>
#endif

#ifdef NOVA_SYNC_HAS_EXPECTED

#    include <concepts>
#    include <mutex>
#    include <system_error>

namespace nova::sync::detail {

template < typename Handler, typename Mutex >
concept invocable_with_expected
    = false // Base case so we can cleanly chain || with macros
#    ifdef NOVA_SYNC_HAS_STD_EXPECTED
      || std::invocable< Handler, std::expected< std::unique_lock< Mutex >, std::error_code > >
#    endif
#    ifdef NOVA_SYNC_HAS_TL_EXPECTED
      || std::invocable< Handler, tl::expected< std::unique_lock< Mutex >, std::error_code > >
#    endif
    ;

} // namespace nova::sync::detail

#endif
