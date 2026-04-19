// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#include <nova/sync/mutex/kqueue_mutex.hpp>

#ifdef NOVA_SYNC_HAS_KQUEUE_MUTEX

#    include <sys/event.h>
#    include <sys/time.h>
#    include <sys/types.h>
#    include <unistd.h>

#    include <cassert>
#    include <cerrno>

namespace nova::sync {

static constexpr uintptr_t kqueue_ident = 1;

kqueue_mutex::kqueue_mutex() :
    kqfd_ {
        ::kqueue(),
    }
{
    assert( kqfd_ >= 0 && "kqueue() failed" );

    struct kevent kev {};
    EV_SET( &kev, kqueue_ident, EVFILT_USER, EV_ADD | EV_CLEAR, NOTE_TRIGGER, 0, nullptr );
    [[maybe_unused]] int r = ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
    assert( r == 0 && "kevent EV_ADD EVFILT_USER failed" );
}

kqueue_mutex::~kqueue_mutex()
{
    if ( kqfd_ >= 0 )
        ::close( kqfd_ );
}

void kqueue_mutex::lock() noexcept
{
    struct kevent out {};
    ::kevent( kqfd_, nullptr, 0, &out, 1, nullptr );
}

bool kqueue_mutex::try_lock() noexcept
{
    struct kevent   out {};
    struct timespec ts { 0, 0 };
    return ::kevent( kqfd_, nullptr, 0, &out, 1, &ts ) > 0;
}

void kqueue_mutex::unlock() noexcept
{
    struct kevent kev {};
    EV_SET( &kev, kqueue_ident, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr );
    ::kevent( kqfd_, &kev, 1, nullptr, 0, nullptr );
}

} // namespace nova::sync

#endif // NOVA_SYNC_HAS_KQUEUE_MUTEX
