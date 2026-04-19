// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/detail/syscall.hpp>

#if defined( __linux__ ) || defined( __APPLE__ )
#    include <unistd.h>
#elif defined( _WIN32 )
#    include <windows.h>
#endif

namespace nova::sync::detail {

#if defined( __linux__ ) || defined( __APPLE__ )
int duplicate_native_handle( int fd ) noexcept
{
    return ::dup( fd );
}
#elif defined( _WIN32 )
void* duplicate_native_handle( void* handle ) noexcept
{
    HANDLE dup_handle = nullptr;
    ::DuplicateHandle( ::GetCurrentProcess(),
                       static_cast< HANDLE >( handle ),
                       ::GetCurrentProcess(),
                       &dup_handle,
                       0,
                       FALSE,
                       DUPLICATE_SAME_ACCESS );
    return dup_handle;
}
#endif

} // namespace nova::sync::detail
