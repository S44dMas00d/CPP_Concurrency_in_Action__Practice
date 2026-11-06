#pragma once
#include <atomic>
#include <iostream>
#include <thread>

template <typename T>
class publish_once {
private:
    std::atomic<T*> ptr_to_instance_T;

public:
    publish_once()
        : ptr_to_instance_T(nullptr)
    {
    }
    ~publish_once()
    {
        delete ptr_to_instance_T;
    }
    publish_once(const publish_once& other) = delete;
    publish_once& operator=(const publish_once& other) = delete;

    // the first call wins: it stores the value and returns true.
    // Every other call returns false right away and changes nothing,
    // even if the winner hasn't finished storing yet.
    bool publish(T value)
    {
        // the reason for having done it this way is, assuming all
        // threads here are racing, when first one touches this call
        // we know that nullptr in expected is the case, so it makes a
        // heap alloc and returns that pointer to the ptr_to_instance_T.
        // I used memory_order_seq_cst because i want immediate visibility
        // of this change made to all threads, even those racing behind the
        // one that made this change.
        // rest, on the second and following calls, we wont compare to nullptr
        // set in expected and each thread gets on call so even if we had used
        // a local `expected` variable to hold nullptr, it wouldnt have made a
        // difference as each thread gets one local copy of that var anyways and
        // since there isnt a while loop - its mutation on failed call wouldnt
        // matter anyways since there is immediate return after the CAS.
        T* expected = nullptr;
        T* desired = new T(std::move(value));
        bool cas_success = ptr_to_instance_T.compare_exchange_strong(expected, desired, std::memory_order_seq_cst);
        if (!cas_success) {
            delete desired;
        }
        return cas_success;
    }
    // nullptr until the value is fully published; afterwards,
    // a pointer to it (the same address on every call, from every thread)
    const T* try_get() const
    {
        // Im using memory_order_acquire here to immediately sync with the store
        // operation carried  out in the publish() function.
        return ptr_to_instance_T.load(std::memory_order_acquire);
    }
    // spins (with std::this_thread::yield()) until published, then
    // returns a reference to the value
    const T& wait() const
    {
        // the while loop is fitted with a load operation that has
        // memory_order_acquire for the same reason that I would want it
        // to pick up the change made by publish as soon as it happens.
        while (!ptr_to_instance_T.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        // the while loop's acquire guarantees that the release mode of seq_cst
        // publish will be made visible to operations below it, therefore, we dont
        // need a stronger memory order here.
        return *ptr_to_instance_T.load(std::memory_order_relaxed);
    }
};