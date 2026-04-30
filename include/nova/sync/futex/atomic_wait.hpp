// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>

namespace nova::sync {

// =============================================================================
// int32_t overloads — primary implementation
// =============================================================================

/// @brief Blocks until the value of @p atom differs from @p old.
///
/// Spurious wakeups are possible; callers must re-check in a loop.
///
/// @param atom   The atomic to monitor.
/// @param old    Expected value; if already different, returns immediately.
/// @param order  Memory ordering (`acquire`, `relaxed`, or `seq_cst`).
void atomic_wait( std::atomic< int32_t >& atom,
                  int32_t                 old,
                  std::memory_order       order = std::memory_order_seq_cst ) noexcept;

/// @brief Blocks until the value differs from @p old or @p rel expires.
///
/// Spurious wakeups are possible; callers must re-check in a loop.
///
/// @param atom   The atomic to monitor.
/// @param old    Expected value; if already different, returns immediately.
/// @param rel    Relative timeout duration.
/// @param order  Memory ordering (`acquire`, `relaxed`, or `seq_cst`).
/// @return `true` if woken (value changed or spurious), `false` on timeout.
bool atomic_wait_for( std::atomic< int32_t >&  atom,
                      int32_t                  old,
                      std::chrono::nanoseconds rel,
                      std::memory_order        order = std::memory_order_seq_cst ) noexcept;

/// @brief Blocks until the value differs from @p old or @p deadline is reached.
/// @param atom     The atomic to monitor.
/// @param old      Expected value.
/// @param deadline Absolute time point.
/// @param order    Memory ordering.
/// @return `true` if woken, `false` on timeout.
bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::steady_clock >& deadline,
                        std::memory_order order = std::memory_order_seq_cst ) noexcept;

/// @brief Blocks until the value differs from @p old or @p deadline is reached.
/// @param atom     The atomic to monitor.
/// @param old      Expected value.
/// @param deadline Absolute time point (system clock / wall time).
/// @param order    Memory ordering.
/// @return `true` if woken, `false` on timeout.
bool atomic_wait_until( std::atomic< int32_t >&                                     atom,
                        int32_t                                                     old,
                        const std::chrono::time_point< std::chrono::system_clock >& deadline,
                        std::memory_order order = std::memory_order_seq_cst ) noexcept;

/// @brief Wakes one thread waiting on this atomic.
/// @param order  Memory ordering (`release` or `seq_cst`).
void atomic_notify_one( std::atomic< int32_t >& atom ) noexcept;

/// @brief Wakes all threads waiting on this atomic.
/// @param order  Memory ordering (`release` or `seq_cst`).
void atomic_notify_all( std::atomic< int32_t >& atom ) noexcept;

// =============================================================================
// uint32_t overloads — thin wrappers over the int32_t core
//
// Futex operates on 32-bit words regardless of signedness. These overloads
// reinterpret the uint32_t atomic as int32_t so callers with unsigned state
// (e.g. fast_mutex, fair_mutex, manual_reset_event) can use the same API
// without manual casts.
// =============================================================================

inline void
atomic_wait( std::atomic< uint32_t >& atom, uint32_t old, std::memory_order order = std::memory_order_seq_cst ) noexcept
{
    atomic_wait( reinterpret_cast< std::atomic< int32_t >& >( atom ), std::bit_cast< int32_t >( old ), order );
}

template < class Clock, class Duration >
inline bool atomic_wait_until( std::atomic< uint32_t >&                          atom,
                               uint32_t                                          old,
                               const std::chrono::time_point< Clock, Duration >& deadline,
                               std::memory_order order = std::memory_order_seq_cst ) noexcept
{
    return atomic_wait_until( reinterpret_cast< std::atomic< int32_t >& >( atom ),
                              std::bit_cast< int32_t >( old ),
                              deadline,
                              order );
}

inline bool atomic_wait_for( std::atomic< uint32_t >& atom,
                             uint32_t                 old,
                             std::chrono::nanoseconds rel,
                             std::memory_order        order = std::memory_order_seq_cst ) noexcept
{
    return atomic_wait_for( reinterpret_cast< std::atomic< int32_t >& >( atom ),
                            std::bit_cast< int32_t >( old ),
                            rel,
                            order );
}

inline void atomic_notify_one( std::atomic< uint32_t >& atom ) noexcept
{
    atomic_notify_one( reinterpret_cast< std::atomic< int32_t >& >( atom ) );
}

inline void atomic_notify_all( std::atomic< uint32_t >& atom ) noexcept
{
    atomic_notify_all( reinterpret_cast< std::atomic< int32_t >& >( atom ) );
}

} // namespace nova::sync
