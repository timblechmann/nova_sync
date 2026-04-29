// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/fast_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

struct counter
{
    mutable nova::sync::fast_mutex mtx;
    int value                      NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

    void increment() NOVA_SYNC_REQUIRES( mtx )
    {
        ++value;
    }
};

int main()
{
    counter c;
    c.increment(); // ERROR: calling REQUIRES function without holding mtx
}
