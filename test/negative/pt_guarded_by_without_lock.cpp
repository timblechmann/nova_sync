// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/annotations.hpp>
#include <nova/sync/mutex/fast_mutex.hpp>

struct container
{
    mutable nova::sync::fast_mutex mtx;
    int* ptr                       NOVA_SYNC_PT_GUARDED_BY( mtx ) { nullptr };
};

int main()
{
    int       value = 0;
    container c;
    c.ptr  = &value;
    *c.ptr = 42; // ERROR: writing through pt_guarded_by pointer without holding mtx
}
