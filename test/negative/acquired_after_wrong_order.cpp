// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/parking_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

struct container
{
    mutable nova::sync::parking_mutex<> mtx;
    int value                           NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

    // Function marked as requiring lock, but called without lock
    void modify() NOVA_SYNC_REQUIRES( mtx )
    {
        ++value;
    }
};

int main()
{
    container c;
    c.modify(); // ERROR: REQUIRES mtx, but mtx is not held
    return 0;
}
