// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/parking_mutex.hpp>
#include <nova/sync/thread_safety/annotations.hpp>

struct counter
{
    mutable nova::sync::parking_mutex<> mtx;
    int value                           NOVA_SYNC_GUARDED_BY( mtx ) { 0 };
};

int main()
{
    counter c;
    ++c.value; // ERROR: accessing guarded member without holding mtx
}
