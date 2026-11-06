#include "fine_grained_queue.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

class slow_type {
private:
    int data;

public:
    slow_type()
        : data(0)
    {
    }
    slow_type(int val)
        : data(val)
    {
    }
    slow_type(slow_type&& other)
        : data(other.data)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    slow_type& operator=(slow_type&& other)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        data = other.data;
        return *this;
    }

    ~slow_type() = default;
};

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

class slow_assign_type {
private:
    int data;

public:
    slow_assign_type()
        : data(0)
    {
    }
    slow_assign_type(int val)
        : data(val)
    {
    }
    // the move constructor stays fast: push() runs it exactly once, so if it
    // slept it would slow push() down regardless of which mutex the pop side
    // is holding, and the test could no longer blame lock contention
    slow_assign_type(slow_assign_type&& other)
        : data(other.data)
    {
    }
    // try_pop(T&) assigns into the caller's object while holding head_mutex.
    // Both assignment flavours sleep so the 200 ms runs whichever one the
    // queue's pop path picks (it currently copy-assigns).
    slow_assign_type& operator=(const slow_assign_type& other)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        data = other.data;
        return *this;
    }
    slow_assign_type& operator=(slow_assign_type&& other)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        data = other.data;
        return *this;
    }

    ~slow_assign_type() = default;
};

int main()
{
    // TEST # 1:
    // Conservation and per-producer order under mixed pops.
    // run many producer and consumer threads concurrently using an atomic flag
    // and then see if TSAN checks any races or if any data loss, overwrites,
    // corruption, torn read, or anything like that is observed.
    fine_grained_queue<int> fine_grained_queue_instance;
    std::vector<std::thread> push_threads(4);
    std::atomic<bool> test_1_start_flag(false);
    for (size_t i = 0; i < push_threads.size(); i++) {
        push_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            int finish = (i + 1) * 10000;
            int curr = finish - 10000;
            while (curr < finish) {
                fine_grained_queue_instance.push(curr);
                curr++;
            }
        });
    }

    std::vector<std::thread> pop_threads(4);
    std::vector<std::vector<int>> storage_vectors_for_popped_vals(pop_threads.size());

    for (size_t i = 0; i < pop_threads.size(); i++) {
        if (i == 0) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                int temp;
                bool got_stop = false;
                do {
                    if (fine_grained_queue_instance.try_pop(temp)) {
                        storage_vectors_for_popped_vals[i].push_back(temp);
                        got_stop = (temp == -1);
                    }

                } while (!got_stop);
            });
        } else if (i == 1) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                std::shared_ptr<int> temp_ptr;
                bool got_stop = false; // TEST # 2:
                do {
                    temp_ptr = fine_grained_queue_instance.try_pop();
                    if (temp_ptr != nullptr) {
                        storage_vectors_for_popped_vals[i].push_back(*temp_ptr);
                        got_stop = (*temp_ptr == -1);
                    }
                } while (!got_stop);
            });
        } else if (i == 2) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                int temp;
                bool got_stop = false;
                do {
                    fine_grained_queue_instance.wait_and_pop(temp);
                    storage_vectors_for_popped_vals[i].push_back(temp);
                    got_stop = (temp == -1);
                } while (!got_stop);
            });
        } else if (i == 3) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                std::shared_ptr<int> temp_ptr;
                bool got_stop = false;
                do {
                    temp_ptr = fine_grained_queue_instance.wait_and_pop();
                    storage_vectors_for_popped_vals[i].push_back(*temp_ptr);
                    got_stop = (*temp_ptr == -1);
                } while (!got_stop);
            });
        }
    }

    // we now need to start the process using the flag:
    test_1_start_flag.store(true);
    // we need to then join all the producers
    for (size_t i = 0; i < push_threads.size(); i++) {
        push_threads[i].join();
    }
    // with the producers joined we assume the values are all done so    // TEST # 2:
    for (size_t i = 0; i < pop_threads.size(); i++) {
        fine_grained_queue_instance.push(-1);
    }
    // then we finally need to end the consumer threads by joining them
    for (size_t i = 0; i < pop_threads.size(); i++) {
        pop_threads[i].join();
    }

    // and now for the asserts.
    std::cout << storage_vectors_for_popped_vals[0].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[1].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[2].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[3].size() << std::endl;
    std::cout << "testing push() function" << std::endl;

    // TEST 1 ASSERT: conservation under mixed pops. Every one of the 40,000
    // produced values must be received exactly once across all four consumers,
    // plus exactly four -1 stop values (one per consumer).
    const int total_pushed = 40000;
    std::vector<int> receive_counts(total_pushed, 0);
    size_t stop_count = 0;
    size_t total_received = 0;
    for (const auto& popped_vals : storage_vectors_for_popped_vals) {
        for (int value : popped_vals) {
            if (value == -1) {
                ++stop_count;
            } else {
                assert(value >= 0 && value < total_pushed
                    && "consumer received a value no producer pushed");
                ++receive_counts[value];
            }
            ++total_received;
        }
    }
    assert(stop_count == 4 && "each consumer should stop at exactly one -1");
    assert(total_received == total_pushed + 4
        && "items were lost or duplicated in transit");
    assert(std::all_of(receive_counts.begin(), receive_counts.end(),
               [](int count) { return count == 1; })
        && "a value was received zero times or more than once");
    std::cout << "TEST 1 PASSED" << std::endl;

    // TEST # 2:
    // A throwing T& pop leaves the item at the head. essentially this test is saying,
    // if your returning from the shared_ptr returning pop methods then even if the
    // constructors are throwing, it doesn't matter because after allocation has been done
    // in the push, shared pointer return is just returning a pointer to the same heap allocation
    // we had already made during the push.
    fine_grained_queue<throwing_type> fine_grained_queue_instance_2;
    throwing_type::throw_on_copy_move = false;

    fine_grained_queue_instance_2.push(throwing_type(1));
    fine_grained_queue_instance_2.push(throwing_type(2));

    throwing_type::throw_on_copy_move = true;

    std::shared_ptr<throwing_type> first = fine_grained_queue_instance_2.try_pop();
    std::shared_ptr<throwing_type> second = fine_grained_queue_instance_2.wait_and_pop();
    assert(first->value == 1 && "constructor shouldn't have re run after its shared ptr was queued");
    assert(second->value == 2 && "constructor shouldn't have re run after its shared ptr was queued");
    std::cout << "TEST 2 PASSED" << std::endl;

    // TEST # 3:
    // A push doesn't wait for a slow pop. Use a type whose move assignment
    // sleeps 200 ms (its move constructor stays fast). Push one item. Thread A
    // calls try_pop(T&); that assignment runs while head_mutex is held. The
    // main thread waits about 50 ms, then times one push() of a new item (pass
    // a temporary). Assert the push returns in well under 50 ms.
    // Catches: push() sharing a mutex with the pop side (the single-mutex
    // design), or taking head_mutex.
    fine_grained_queue<slow_assign_type> slow_queue_instance;
    slow_queue_instance.push(slow_assign_type {});

    std::thread popper_thread([&]() {
        slow_assign_type popped;
        slow_queue_instance.try_pop(popped);
    });

    // give the popper time to grab head_mutex and start the 200 ms assignment
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto push_start = std::chrono::steady_clock::now();
    slow_queue_instance.push(slow_assign_type {});
    const auto push_elapsed = std::chrono::steady_clock::now() - push_start;

    popper_thread.join();

    // push() only needs tail_mutex, which the pop side holds only for an
    // instant; if push shared head_mutex (single-mutex design) or took it, it
    // would block for the ~150 ms remaining in the pop's assignment
    assert(push_elapsed < std::chrono::milliseconds(50)
        && "push() waited on the pop side: it shares a mutex with try_pop or takes head_mutex");
    std::cout << "TEST 3 PASSED" << std::endl;

    return 0;
}
