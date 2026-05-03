// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/detail/pause.hpp>

#include <thread>

namespace nova::sync::detail {

struct exponential_backoff
{
    int                  backoff    = 2;
    // Spin up to 16384 cycles before yielding (tuned via benchmarking for typical workloads)
    static constexpr int spin_limit = 1 << 14;

    void run()
    {
        if ( backoff < spin_limit ) {
            for ( int i = 0; i < backoff; ++i )
                detail::pause();
            backoff *= 2; // double backoff for next time
        } else {
            std::this_thread::yield();
        }
    }
};

enum class backoff_result : uint8_t
{
    success,
    retry,
    retry_without_backoff
};

template < typename Functor >
    requires( std::is_same_v< std::invoke_result_t< Functor >, backoff_result > )
[[nodiscard]] bool run_with_exponential_backoff_until( Functor&& func )
{
    exponential_backoff backoff;

    do {
        backoff_result state = func();
        switch ( state ) {
        case backoff_result::success:               return true;
        case backoff_result::retry_without_backoff: continue;
        case backoff_result::retry:                 {
            backoff.run();
            break;
        }
        }
    } while ( backoff.backoff < exponential_backoff::spin_limit );
    return false;
}

} // namespace nova::sync::detail
