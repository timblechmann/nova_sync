// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <new>
#include <version>

#if __has_cpp_attribute( gnu::noinline )
#  define NOVA_SYNC_NOINLINE [[gnu::noinline]]
#elif __has_cpp_attribute( msvc::noinline ) // C++23
#  define NOVA_SYNC_NOINLINE [[msvc::noinline]]
#else
#  define NOVA_SYNC_NOINLINE
#endif

namespace nova::sync::detail {

#ifdef __cpp_lib_hardware_interference_size
using std::hardware_destructive_interference_size;
#else
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

} // namespace nova::sync::detail
