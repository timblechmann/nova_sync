// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#if defined( __x86_64__ ) || defined( _M_X64 ) || defined( __i386__ ) || defined( _M_IX86 )
#  include <immintrin.h>
#endif

#ifdef _MSC_VER
#  include <intrin.h>
#endif

namespace nova::sync::detail {

inline void pause()
{
#if defined( __x86_64__ ) || defined( _M_X64 ) || defined( __i386__ ) || defined( _M_IX86 )
    _mm_pause();
#elif defined( __arm64__ ) || defined( __aarch64__ ) || defined( __arm__ ) || defined( _M_ARM64 ) || defined( _M_ARM )
#  if defined( _MSC_VER )
#    if defined( _ARM_BARRIER_SY )
#      define NOVA_SYNC__ARM_BARRIER_SY _ARM_BARRIER_SY
#    else
#      define NOVA_SYNC__ARM_BARRIER_SY 0xF
#    endif
    __isb( NOVA_SYNC__ARM_BARRIER_SY );
#    undef NOVA_SYNC__ARM_BARRIER_SY
#  else
    __asm__ __volatile__( "isb" ::
                              : "memory" );
#  endif
#else
    ( (void)0 );
#endif
}

} // namespace nova::sync::detail
