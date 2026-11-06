#pragma once
#include <atomic>
#include <thread>

class spinlock_mutex {
private:
    std::atomic_flag flag;

public:
    spinlock_mutex()
        : flag(ATOMIC_FLAG_INIT)
    {
    }

    void lock()
    {
        // on returning false the while loop breaks and therefore
        // busy wait on this thread ends and the thread
        // now holds the lock and can proceed to lines below
        // this busy wait lock() function
        while (flag.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock()
    {
        flag.clear(std::memory_order_release);
    }
};