// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/semaphore/dispatch_semaphore.hpp>

#ifdef NOVA_SYNC_HAS_DISPATCH_SEMAPHORE

#  include <cassert>

namespace nova::sync {

dispatch_semaphore::dispatch_semaphore( std::ptrdiff_t initial )
{
    // dispatch_release requires the counter to match the creation value.
    // Work around by creating with 0 and signaling `initial` times.
    sem_ = dispatch_semaphore_create( 0 );
    assert( sem_ != nullptr && "dispatch_semaphore_create failed" );

    for ( std::ptrdiff_t i = 0; i < initial; ++i )
        dispatch_semaphore_signal( sem_ );
}

dispatch_semaphore::~dispatch_semaphore()
{
    dispatch_release( sem_ );
}

void dispatch_semaphore::release( std::ptrdiff_t n ) noexcept
{
    assert( n >= 0 && "dispatch_semaphore::release: n must be non-negative" );
    for ( std::ptrdiff_t i = 0; i < n; ++i )
        dispatch_semaphore_signal( sem_ );
}

void dispatch_semaphore::acquire() noexcept
{
    dispatch_semaphore_wait( sem_, DISPATCH_TIME_FOREVER );
}

bool dispatch_semaphore::try_acquire() noexcept
{
    return dispatch_semaphore_wait( sem_, DISPATCH_TIME_NOW ) == 0;
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_DISPATCH_SEMAPHORE
