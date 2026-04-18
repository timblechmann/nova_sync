// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __x86_64__ ) || defined( _M_X64 )
#    include <immintrin.h>
#endif

#ifdef _MSC_VER
#    include <intrin.h>
#endif

namespace nova::sync::detail {

inline void pause()
{
#if defined( __x86_64__ ) || defined( _M_X64 )
    _mm_pause();
#elif defined( __arm64__ ) || defined( __aarch64__ ) && defined( __GNUC__ )
    __builtin_arm_isb( 0xF );
#elif defined( _MSC_VER )
    __isb( _ARM_BARRIER_SY );
#else
    ( (void)0 )
#endif
}

} // namespace nova::sync::detail
