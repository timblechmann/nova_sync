// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/fast_mutex.hpp>
#include <nova/sync/mutex/tsa_annotations.hpp>

struct container
{
    mutable nova::sync::fast_mutex mtx;
    int value                      NOVA_SYNC_GUARDED_BY( mtx ) { 0 };

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
