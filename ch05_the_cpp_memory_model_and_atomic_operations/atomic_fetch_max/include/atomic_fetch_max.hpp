#pragma once
#include <atomic>
#include <thread>
// ARCHITECTURE ON THIS PC IS:
// $ uname -m
// x86_64
// $ lscpu
// Architecture:                x86_64
//   CPU op-mode(s):            32-bit, 64-bit
//   Address sizes:             48 bits physical, 48 bits virtual
//   Byte Order:                Little Endian
// CPU(s):                      16
//   On-line CPU(s) list:       0-15

// this means - for this x86-64 machine: strong() is NOT
// meaningfully more expensive than weak(). The classic
// reason for preferring weak() is particularly relevant
// to architectures such as ARM.

template <typename T>
T fetchMax(std::atomic<T>& target, T value, int& win_count)
{
    // NOTE: race == possible race here between callers
    // race
    T old = target.load(std::memory_order_seq_cst);
    // race
    while (old < value) {
        // race
        if (target.compare_exchange_strong(old, value,
                std::memory_order_seq_cst)) {
            win_count++;
            break;
        }
    }
    // race
    return old;
}

template <typename T>
T fetchMax(std::atomic<T>& target, T value)
{
    // NOTE: race == possible race here between callers
    // race
    T old = target.load(std::memory_order_seq_cst);
    // race
    while (old < value) {
        // race
        if (target.compare_exchange_strong(old, value,
                std::memory_order_seq_cst)) {
            break;
        }
    }
    // race
    return old;
}
