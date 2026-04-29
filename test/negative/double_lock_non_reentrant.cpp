// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/fast_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

struct container
{
    mutable nova::sync::fast_mutex mtx;
    int value                      NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

    void nested_lock() NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        mtx.lock(); // ERROR: Non-reentrant mutex locked twice (deadlock risk)
        ++value;
        mtx.unlock();
        mtx.unlock();
    }
};

int main()
{
    container c;
    c.nested_lock();
}
