#include "ref_counted_stack.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

class test_type {
private:
    int value;

public:
    inline static std::atomic<int> no_of_instances;
    test_type(const int value_)
        : value(value_)
    {
        ++no_of_instances;
    };
    test_type(const test_type& other)
        : value(other.value)
    {
        ++no_of_instances;
    };
    ~test_type()
    {
        --no_of_instances;
    };
};

int main()
{
    // TEST # 1:
    // Every value comes out exactly once under contention. 4 pushers each push
    // 20,000 distinct ints, while 4 poppers pop until 80,000 successful pops in
    // total. Assert every value was received exactly once. Catches: lost or
    // duplicated nodes, a node freed while still in use (ASan use-after-free),
    // or freed twice (ASan double-free).

    const int total_pushed = 4 * 20000;
    ref_counted_stack<int> ref_counted_stack_instance_1;
    std::vector<std::thread> push_threads(4);
    std::atomic<int> test_1_popped_count(0);
    std::atomic<bool> test_1_start_flag(false);

    for (size_t i = 0; i < push_threads.size(); i++) {
        push_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            int finish = (i + 1) * 20000;
            int curr = finish - 20000;
            while (curr < finish) {
                ref_counted_stack_instance_1.push(curr);
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
                std::shared_ptr<int> temp_ptr = ref_counted_stack_instance_1.pop();
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
    // Exactly one winner per item. For 500 rounds: push one value (the round
    // number), release 4 poppers together, each calling pop() once. Assert
    // exactly one non-null result per round, equal to the round number, and
    // three nullptrs.
    ref_counted_stack<int> ref_counted_stack_instance_2;
    std::atomic<int> total_pushes_to_instance_2(0);
    std::atomic<bool> push_done(false);
    std::atomic<bool> test_2_start_flag(false);

    std::thread push_thread_2 = std::thread([&]() {
        while (!test_2_start_flag.load()) { };
        while (total_pushes_to_instance_2.load() < 500) {
            bool expected = false;
            bool desired = true;
            ref_counted_stack_instance_2.push(total_pushes_to_instance_2.load());
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
                std::shared_ptr<int> temp_ptr = ref_counted_stack_instance_2.pop();
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
    while (std::shared_ptr<int> leftover = ref_counted_stack_instance_2.pop()) {
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

    // Destroying a non-empty stack frees everything. Use a type that counts live
    // instances: the constructor and copy constructor increment a global counter,
    // and the destructor decrements it. Push 10 values, then assert the counter
    // is 10: only the stack's copies are alive. Destroy the stack without popping,
    // and assert the counter is 0. Catches: a destructor that leaks nodes or their
    // data (F16's leak).
    ref_counted_stack<test_type>* ref_counted_stack_instance_3
        = new ref_counted_stack<test_type>();
    for (int i = 0; i < 10; i++) {
        ref_counted_stack_instance_3->push(test_type(i));
    }
    assert(test_type::no_of_instances == 10
        && "only the stack's copies ought to be alive after pushing");

    delete ref_counted_stack_instance_3;

    assert(test_type::no_of_instances == 0
        && "all instances ought to have been deleted in the destruction of stack");

    std::cout << "TEST 3 PASSED" << std::endl;
    return 0;
}
