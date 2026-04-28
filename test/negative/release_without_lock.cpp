// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/fast_mutex.hpp>
#include <nova/sync/mutex/tsa_annotations.hpp>

struct unlocked_caller
{
    mutable nova::sync::fast_mutex mtx;

    void unlock_without_lock() NOVA_SYNC_REQUIRES( mtx )
    {
        // This function expects 'mtx' to be held on entry
    }
};

int main()
{
    unlocked_caller c;
    c.unlock_without_lock(); // ERROR: Calling REQUIRES function without holding mtx
}
