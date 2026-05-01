// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <nova/sync/detail/pause.hpp>

#include <thread>

namespace nova::sync::detail {

struct exponential_backoff
{
    int                  backoff    = 8;
    static constexpr int spin_limit = 1 << 12;

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

} // namespace nova::sync::detail
