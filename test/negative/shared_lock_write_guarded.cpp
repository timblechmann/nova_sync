// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/shared_spinlock_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

struct shared_guarded
{
    mutable nova::sync::shared_spinlock_mutex<> mtx;
    int value                                   NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

    void read_only() const NOVA_SYNC_REQUIRES_SHARED( mtx )
    {
        // This is OK — reading
        int v = value;
        (void)v;
    }

    void write_while_shared_held() NOVA_SYNC_EXCLUDES( mtx )
    {
        mtx.lock_shared();
        ++value; // ERROR: Write to GUARDED_BY while only holding shared lock
        mtx.unlock_shared();
    }
};

int main()
{
    shared_guarded g;
    g.write_while_shared_held();
}
