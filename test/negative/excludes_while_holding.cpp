// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/parking_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

struct counter
{
    mutable nova::sync::parking_mutex<> mtx;
    int value                           NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

    void increment() NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock();
        ++value;
        mtx.unlock();
    }
};

int main()
{
    counter c;
    c.mtx.lock();
    c.increment(); // ERROR: calling EXCLUDES function while holding mtx
    c.mtx.unlock();
}
