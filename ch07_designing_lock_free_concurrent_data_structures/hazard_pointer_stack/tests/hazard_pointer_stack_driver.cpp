#include "hazard_pointer_stack.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

class test_type_w_counter {
public:
    inline static std::atomic<int> del_counter;

    test_type_w_counter() = default;
    ~test_type_w_counter()
    {
        ++del_counter;
    }
};

int main()
{

    // TEST # 1:
    // Every value comes out exactly once under contention.
    // As in lock_free_stack Test 1: 4 pushers each push 20,000 distinct ints,
    // while 4 poppers pop until 80,000 successful pops in total. Assert every
    // value was received exactly once. Catches: lost or duplicated nodes, and
    // a node deleted while another thread can still dereference it (ASan reports
    // use-after-free).
    const int total_pushed = 4 * 20000;
    hazard_pointer_stack<int> hazard_pointer_stack_instance_1;
    std::vector<std::thread> push_threads(4);
    std::atomic<int> test_1_popped_count(0);
    std::atomic<bool> test_1_start_flag(false);

    for (size_t i = 0; i < push_threads.size(); i++) {
        push_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            int finish = (i + 1) * 20000;
            int curr = finish - 20000;
            while (curr < finish) {
                hazard_pointer_stack_instance_1.push(curr);
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
                std::shared_ptr<int> temp_ptr = hazard_pointer_stack_instance_1.pop();
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
    // A protected object isn't deleted until its hazard pointer is cleared, and
    // then it's deleted with the right type. Use the utility directly, with a
    // small type whose destructor increments a global counter.
    // - Create one object with new.
    // - Thread A sets its hazard pointer to that object, signals the main thread,
    //   and waits.
    // - The main thread calls reclaim_later(obj) and then
    //   delete_nodes_with_no_hazards(). Assert the counter is still 0.
    // - Signal A to clear its hazard pointer, wait until it has, and call
    //   delete_nodes_with_no_hazards() again. Assert the counter is exactly 1.
    // Catches: deleting without checking other threads' hazard pointers, deleting
    // through void* (the destructor never runs), and never deleting.
    std::atomic<bool> hp_is_set(false);
    std::atomic<bool> signal_to_release(false);
    std::atomic<bool> signal_of_release(false);
    test_type_w_counter* test_type_w_counter_instance_ptr = new test_type_w_counter;
    std::thread thread_A = std::thread([&]() {
        std::atomic<void*>& hp = hazard_ptr::get_hazard_pointer_for_current_thread();
        hp.store(test_type_w_counter_instance_ptr);
        hp_is_set.store(true);
        while (!signal_to_release.load()) { };
        hp.store(nullptr);
        signal_of_release.store(true);
    });
    while (!hp_is_set.load()) { };
    hazard_ptr::reclaim_later(test_type_w_counter_instance_ptr);
    hazard_ptr::delete_nodes_with_no_hazards();
    assert(test_type_w_counter::del_counter.load() == 0
        && "deletion counter ought to be 0 still because hp to this object is active.");
    signal_to_release.store(true);
    while (!signal_of_release.load()) { };
    hazard_ptr::delete_nodes_with_no_hazards();
    assert(test_type_w_counter::del_counter.load() == 1
        && "deletion counter ought to be 1 because hp to this object is NO longer active.");
    // join() is the LAST interaction with a thread: it only blocks until the
    // thread finishes (the thread started running at construction, not here).
    // Thread A can't finish until signal_to_release is set above, so joining
    // any earlier would deadlock -- main waiting for A, A waiting for main.
    thread_A.join();
    std::cout << "TEST 2 PASSED" << std::endl;

    // TEST # 3:
    // Threads that exit give their slot back.
    // Run 300 short-lived threads one after another (3 × max_hazard_pointers). Each
    // calls get_hazard_pointer_for_current_thread(), stores the address of one shared
    // global object into it, and exits. Assert none of them threw. After the last has
    // been joined, assert outstanding_hazard_pointers_for(&that_object) is false.
    // Catches: a slot not released at thread exit (the 101st thread throws "No hazard
    // pointers available"), and a pointer left set after its thread is gone.
    int* test_integer_obj = new int(10);
    bool threw = false;
    try {

        for (int i = 0; i < 300; i++) {
            std::thread t([&]() {
                std::atomic<void*>& hp = hazard_ptr::get_hazard_pointer_for_current_thread();
                hp.store(test_integer_obj);
            });
            t.join();
        }
    } catch (const std::exception& e) {
        threw = true;
        std::cerr << e.what() << '\n';
    }

    assert(!threw && "none of the short lived threads were supposed to throw");

    assert(!hazard_ptr::outstanding_hazard_pointers_for(test_integer_obj)
        && "on thread release - the hazard pointer entry should also have release from the RAII hp_owner obj");
    std::cout << "TEST 3 PASSED" << std::endl;

    return 0;
}