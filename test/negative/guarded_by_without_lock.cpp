// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/annotations.hpp>
#include <nova/sync/mutex/fast_mutex.hpp>

struct counter
{
    mutable nova::sync::fast_mutex mtx;
    int value                      NOVA_SYNC_GUARDED_BY( mtx ) { 0 };
};

int main()
{
    counter c;
    ++c.value; // ERROR: accessing guarded member without holding mtx
}
