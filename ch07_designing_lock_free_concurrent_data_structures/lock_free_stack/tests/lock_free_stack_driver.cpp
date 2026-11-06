#include "lock_free_stack.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

class throwing_type {

public:
    inline static bool throw_on_copy_move;
    int value;
    throwing_type(int v)
        : value(v) {};

    throwing_type(const throwing_type& other)
    {
        if (throw_on_copy_move) {
            throw std::runtime_error("copy construction");
        }
        value = other.value;
    }

    throwing_type(const throwing_type&& other)
    {
        if (throw_on_copy_move) {
            throw std::runtime_error("move construction");
        }
        value = other.value;
    }

    throwing_type& operator=(const throwing_type& other)
    {
        if (throw_on_copy_move) {
            throw std::runtime_error("copy assignment");
        }
        value = other.value;
        return *this;
    }

    throwing_type& operator=(throwing_type&& other)
    {
        if (throw_on_copy_move) {
            throw std::runtime_error("copy assignment");
        }
        value = other.value;
        return *this;
    }

    ~throwing_type() = default;
};

int main()
{
    // TEST # 1:
    // Every value comes out exactly once under contention. 4 pusher
    // threads each push 20,000 distinct ints (pusher p pushes
    // p*20000 + k). At the same time, 4 popper threads each pop in a
    // loop, retrying on nullptr, until a shared atomic count of
    // successful pops reaches 80,000. Assert every one of the 80,000
    // values was received exactly once, across all poppers. Catches:
    // lost or duplicated nodes from a wrong CAS loop, and
    // use-after-free in reclamation (ASan or TSAN flags it).
    const int total_pushed = 4 * 20000;
    lock_free_stack<int> lock_free_stack_instance;
    std::vector<std::thread> push_threads(4);
    std::atomic<bool> test_1_start_flag(false);
    std::atomic<int> test_1_popped_count(0);

    for (size_t i = 0; i < push_threads.size(); i++) {
        push_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            int finish = (i + 1) * 20000;
            int curr = finish - 20000;
            while (curr < finish) {
                lock_free_stack_instance.push(curr);
                curr++;
            }
        });
    }
    std::vector<std::thread> pop_threads(push_threads.size());
    std::vector<std::vector<int>>
        storage_vectors_for_popped_vals(pop_threads.size());

    for (size_t i = 0; i < pop_threads.size(); i++) {
        pop_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            while (test_1_popped_count.load() < total_pushed) {
                std::shared_ptr<int> temp_ptr = lock_free_stack_instance.pop();
                if (temp_ptr) {
                    storage_vectors_for_popped_vals[i].push_back(*temp_ptr);
                    ++test_1_popped_count;
                }
            }
        });
    }

    test_1_start_flag.store(true);
    for (size_t i = 0; i < push_threads.size(); i++) {
        push_threads[i].join();
    }
    for (size_t i = 0; i < pop_threads.size(); i++) {
        pop_threads[i].join();
    }
    // and now for the asserts.
    std::cout << storage_vectors_for_popped_vals[0].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[1].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[2].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[3].size() << std::endl;

    std::vector<int> receive_counts(total_pushed, 0);
    int total_received = 0;
    for (const auto& popped_vals : storage_vectors_for_popped_vals) {
        for (int value : popped_vals) {
            assert(value >= 0 && value < total_pushed
                && "consumer received a value no producer pushed");
            ++receive_counts[value];
            ++total_received;
        }
    }

    assert(total_received == total_pushed
        && "items seem to have been lost or duplicated in transit");
    assert(std::all_of(receive_counts.begin(), receive_counts.end(),
               [](int count) { return count == 1; })
        && "a value was received 0 times or more than 1 times");
    std::cout << "TEST 1 PASSED" << std::endl;

    // TEST # 2:
    // One item, many poppers: exactly one winner. For 500 rounds,
    // push one value (the round number), then release 4 popper
    // threads together, each calling pop() once. Reusing the same 4 threads across rounds is fine. Assert exactly one thread got a non-null result, it equals the round number, and the other three got nullptr. Catches: a pop() that returns a node without winning the exchange (two threads return the same node), and dereferencing a null head on an empty stack.
    lock_free_stack<int> lock_free_stack_instance_2;
    std::atomic<int> total_pushes_to_instance_2(0);
    std::atomic<bool> push_done(false);
    std::atomic<bool> test_2_start_flag(false);

    std::thread push_thread_2 = std::thread([&]() {
        while (!test_2_start_flag.load()) { };
        while (total_pushes_to_instance_2.load() < 500) {
            bool expected = false;
            bool desired = true;
            lock_free_stack_instance_2.push(total_pushes_to_instance_2.load());
            ++total_pushes_to_instance_2;
            while (!push_done.compare_exchange_strong(expected, desired)) {
                expected = false;
            }
        }
    });

    std::vector<std::thread> pop_threads_2(4);
    std::vector<std::vector<int>>
        storage_vectors_for_popped_vals_2(pop_threads_2.size());

    for (size_t i = 0; i < pop_threads_2.size(); i++) {
        pop_threads_2[i] = std::thread([&, i]() {
            while (!test_2_start_flag.load()) { };
            while (total_pushes_to_instance_2.load() < 500) {
                bool expected = true;
                bool desired = false;
                while (total_pushes_to_instance_2.load() < 500 && !push_done.compare_exchange_strong(expected, desired)) {
                    expected = true;
                }
                std::shared_ptr<int> temp_ptr = lock_free_stack_instance_2.pop();
                if (temp_ptr) {
                    storage_vectors_for_popped_vals_2[i].push_back(*temp_ptr);
                }
            }
        });
    }

    test_2_start_flag.store(true);

    push_thread_2.join();
    for (size_t i = 0; i < pop_threads_2.size(); i++) {
        pop_threads_2[i].join();
    }
    // The gating protocol can leave a pushed value in the stack: round 0's
    // push_done hand-off is consumed by the pusher itself, and poppers exit
    // as soon as the push count reaches 500. Drain whatever remains so the
    // asserts see every pushed value exactly once.
    while (std::shared_ptr<int> leftover = lock_free_stack_instance_2.pop()) {
        storage_vectors_for_popped_vals_2[0].push_back(*leftover);
    }
    // and now for the asserts.
    std::cout << storage_vectors_for_popped_vals_2[0].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals_2[1].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals_2[2].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals_2[3].size() << std::endl;

    const int test_2_rounds = 500;
    std::vector<int> receive_counts_2(test_2_rounds, 0);
    int total_received_2 = 0;
    for (const auto& popped_vals : storage_vectors_for_popped_vals_2) {
        for (int value : popped_vals) {
            assert(value >= 0 && value < test_2_rounds
                && "popper received a value the pusher never pushed");
            ++receive_counts_2[value];
            ++total_received_2;
        }
    }
    assert(total_received_2 == test_2_rounds
        && "items seem to have been lost or duplicated in transit");
    assert(std::all_of(receive_counts_2.begin(), receive_counts_2.end(),
               [](int count) { return count == 1; })
        && "a value was received 0 times or more than 1 times");
    std::cout << "TEST 2 PASSED" << std::endl;

    // TEST # 3:
    // A throwing push leaves the stack unchanged. Use a type whose copy
    // constructor throws while a global flag is set. Push 1 and 2 with the
    // flag off, then turn it on. Assert that push(3) throws. Turn the flag
    // off, then assert pop() gives 2, then 1, then nullptr. Catches: linking a
    // node into the stack before its data has been built.
    lock_free_stack<throwing_type> lock_free_stack_instance_3;
    throwing_type::throw_on_copy_move = false;

    lock_free_stack_instance_3.push(throwing_type(1));
    lock_free_stack_instance_3.push(throwing_type(2));

    throwing_type::throw_on_copy_move = true;

    bool threw = false;
    try {
        lock_free_stack_instance_3.push(throwing_type(3));
    } catch (const std::runtime_error&) {
        threw = true;
    }

    assert(threw
        && "once the static for throwing constructor was turned on, it should have thrown");

    throwing_type::throw_on_copy_move = false;

    lock_free_stack_instance_3.pop();
    lock_free_stack_instance_3.pop();
    assert(lock_free_stack_instance_3.pop() == nullptr && "this should have returned nullptr since we only pushed 2 numbers");
    std::cout << "TEST 3 PASSED" << std::endl;

    std::cout << "ALL TESTS PASSED" << std::endl;
    return 0;
}
