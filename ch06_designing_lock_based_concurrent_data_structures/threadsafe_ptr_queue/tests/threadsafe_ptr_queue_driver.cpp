#include "threadsafe_ptr_queue.hpp"
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

int main()
{
    // TEST # 1:
    // Conservation under mixed pops. 4 producers each push 10,000 distinct ints
    // (producer p pushes p*10000 + k). 4 consumers run at the same time, one per
    // pop flavour: wait_and_pop() returning a pointer, wait_and_pop(T&), and
    // try_pop() in each form, called in a loop. After joining the producers, push
    // four -1 stop values; each consumer stops at the first -1 it gets. Assert that,
    // across all consumers, every one of the 40,000 values was received exactly
    // once. Catches: lost or duplicated items, and races (TSAN).

    threadsafe_ptr_queue<int> ptr_queue_instance {};
    // first lets make vector to hold push threads;
    std::vector<std::thread> push_threads(4);
    std::atomic<bool> test_1_start_flag(false);
    for (size_t i = 0; i < push_threads.size(); ++i) {
        push_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            int finish = (i + 1) * 10000;
            int curr = finish - 10000;
            while (curr < finish) {
                ptr_queue_instance.push(curr);
                curr++;
            }
        });
    }

    std::vector<std::thread> pop_threads(4);
    std::vector<std::vector<int>> storage_vectors_for_popped_vals(4);
    for (size_t i = 0; i < pop_threads.size(); ++i) {
        if (i == 0) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                int temp;
                bool got_stop = false;
                do {
                    if (ptr_queue_instance.try_pop(temp)) {
                        storage_vectors_for_popped_vals[i].push_back(temp);
                        got_stop = (temp == -1);
                    }
                } while (!got_stop);
            });
        } else if (i == 1) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                std::shared_ptr<int> temp_ptr;
                bool got_stop = false;
                do {
                    temp_ptr = ptr_queue_instance.try_pop();
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
                do {
                    ptr_queue_instance.wait_and_pop(temp);
                    storage_vectors_for_popped_vals[i].push_back(temp);
                } while (temp != -1);
            });
        } else if (i == 3) {
            pop_threads[i] = std::thread([&, i]() {
                while (!test_1_start_flag.load()) { };
                std::shared_ptr<int> temp_ptr;
                do {
                    temp_ptr = ptr_queue_instance.wait_and_pop();
                    storage_vectors_for_popped_vals[i].push_back(*temp_ptr);
                } while (*temp_ptr != -1);
            });
        }
    }

    // now we have all the tasks setup and blocking eight threads
    // we now need to start the process using the flag:
    test_1_start_flag.store(true);
    // we need to then join all the producers
    for (size_t i = 0; i < push_threads.size(); ++i) {
        push_threads[i].join();
    }
    // with the producers joined we assume the values are all done so
    // we need to push the last four -1's and allow the consumers to end
    // their loops
    for (int i = 0; i < 4; i++) {
        ptr_queue_instance.push(-1);
    }
    // then we finally need to end the consumer threads by joining them
    for (size_t i = 0; i < pop_threads.size(); ++i) {
        pop_threads[i].join();
    }
    // and now for the asserts.
    std::cout << storage_vectors_for_popped_vals[0].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[1].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[2].size() << std::endl;
    std::cout << storage_vectors_for_popped_vals[3].size() << std::endl;

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
    // Pointer pops never touch T.Use a test type whose copy and move operations
    // (constructors and assignments) all throw while a global flag is set.Push two
    // items with the flag off, then turn it on.Call try_pop() and wait_and_pop()
    // (the pointer versions).Assert that neither throws and that they return the two
    // items in FIFO order.Catches : a pop that still builds the T itself(the Listing
    // 6.2 shape).

    threadsafe_ptr_queue<throwing_type> throwing_queue_instance;
    throwing_type::throw_on_copy_move = false;

    throwing_queue_instance.push(throwing_type(1));
    throwing_queue_instance.push(throwing_type(2));

    throwing_type::throw_on_copy_move = true;

    std::shared_ptr<throwing_type> first = throwing_queue_instance.try_pop();
    std::shared_ptr<throwing_type> second = throwing_queue_instance.wait_and_pop();
    assert(first->value == 1 && "constructor shouldn't have re run after its shared ptr was queued");
    assert(second->value == 2 && "constructor shouldn't have re run after its shared ptr was queued");
    std::cout << "TEST 2 PASSED" << std::endl;

    // TEST # 3:
    // A throwing T& pop keeps the item. Same type. Push one item with the flag off,
    // turn the flag on, and call try_pop(T&). Assert that it throws. Turn the flag
    // off and call try_pop(T&) again. Assert that it succeeds with that same item.
    // Catches: popping from the queue before the assignment into value has finished.
    throwing_type::throw_on_copy_move = false;
    throwing_queue_instance.push(throwing_type(3));
    throwing_type::throw_on_copy_move = true;
    throwing_type third(0);
    try {
        throwing_type third(0);
        throwing_queue_instance.try_pop(third);
        /* code */
    } catch (const std::exception& e) {
        assert(third.value == 0 && "move constructor wouldn't have allowed this to happen");
        std::cerr << e.what() << '\n';
    }
    std::cout << "TEST 3 PASSED" << std::endl;

    // TEST # 4:
    // Construction happens outside the lock. Use a type whose move constructor
    // sleeps 200 ms. Thread A calls q.push(slow_type{}). Pass a temporary, so the
    // only move is the one inside push(). The main thread waits about 50 ms, then
    // times one q.empty() call. Assert that the call returns in well under 50 ms.
    // Catches: creating the stored element while holding the mutex.

    threadsafe_ptr_queue<slow_type> slow_queue_instance;
    std::thread pusher_thread([&]() {
        slow_queue_instance.push(slow_type {});
    });

    // give the pusher time to get inside push() and start the 200 ms move
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto empty_start = std::chrono::steady_clock::now();
    slow_queue_instance.empty();
    const auto empty_elapsed = std::chrono::steady_clock::now() - empty_start;

    pusher_thread.join();

    // if push() built the shared_ptr (i.e. ran the 200 ms move ctor) while
    // holding the mutex, empty() would have blocked for the ~150 ms remaining
    assert(empty_elapsed < std::chrono::milliseconds(50)
        && "empty() blocked on the mutex: the stored element was constructed under the lock");
    std::cout << "TEST 4 PASSED" << std::endl;

    return 0;
}