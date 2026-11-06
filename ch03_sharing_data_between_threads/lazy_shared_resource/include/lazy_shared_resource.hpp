#pragma once
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

// lazy_shared_resource — build task for section 3.3.1 (Protecting shared
// data during initialization).
//
// Interface, contract, and edge cases: see
// ch03_sharing_data_between_threads/alternative-facilities-for-protecting-shared-data-notes.md.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// class/function(s) yourself, per the framework's rule that Claude never
// writes the implementation. You choose the initialization mechanism
// (std::call_once/std::once_flag, or a function-local static) — that
// choice is the design-judgment part of this task.
//
// Reminder: this header will be included by multiple translation units once
// linked into a real build. Any out-of-class member function definitions you
// write here must be `inline` — same ODR lesson from thread_guard (2.1) and
// account_transfer (3.2.4).

class expensive_resource {
private:
    std::mutex mt;
    int initialization_count;
    int access_count;
    int mutation_count;
    struct state {
        int initialization_count;
        int access_count;
        int mutation_count;
    };

    // INLINE BECAUSE:
    // C++17 inline static members provide the definition in the class,
    // so no separate definition is required in the .cpp file.
    // STATIC BECAUSE:
    // Static so it exists independently of any MyClass instance;
    // it stores the pointer to the single instance created by get_instance().
    inline static std::once_flag instance_initialization_flag;
    inline static expensive_resource* single_instance;

    // "expensive" — simulate with a short sleep_for
    // in the constructor to widen the race window
    // for the stress test below, and increment a
    // shared, atomic construction counter so the
    // test can verify "constructed exactly once"
    expensive_resource();

public:
    static void accessor(expensive_resource*& pointer_holder);

    // anything trivial that proves the object is
    // actually usable, not just non-null
    void do_something();

    // since the member are private, we ought to be able to print their
    // states in the terminal
    void print_state();

    // since the member are private, we ought to be able to copy their states
    // to a caller
    state return_state();
};
expensive_resource::expensive_resource()
{
    initialization_count = 1;
    mutation_count = 0;
    access_count = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

// Accessor shape (shared_ptr, reference, whatever you prefer) — thread-safe,
// lazy, exactly-once construction, cheap on every call after the first.

void expensive_resource::accessor(expensive_resource*& pointer_holder)
{
    std::call_once(instance_initialization_flag, [] {
        single_instance = new expensive_resource();
    });
    {
        // had to add this here otherwise was getting a race
        // from 50 threads on this one single_instance->access_count
        std::lock_guard<std::mutex> lk(single_instance->mt);
        single_instance->access_count += 1;
    }

    pointer_holder = single_instance;
}

// anything trivial that proves the object is
// actually usable, not just non-null
// lets make it increment the mutation count
// can only call it using the reference to the single
// static pointer of the one class instance.
void expensive_resource::do_something()
{
    std::lock_guard<std::mutex> lk(mt);
    mutation_count += 1;
}

// since the member are private, we ought to be able to print their
// states in the terminal
void expensive_resource::print_state()
{
    std::cout << "initialization_count = " << initialization_count
              << ", access_count = " << access_count
              << ", mutation_count = " << mutation_count << std::endl;
}

// since the member are private, we ought to be able to copy their states
// to a caller
expensive_resource::state expensive_resource::return_state()
{
    return {
        initialization_count,
        access_count,
        mutation_count,
    };
}