#include "threadsafe_list.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main()
{
    // TEST # 1:
    // Concurrent pushes and removals keep exactly the right items. 4 pusher
    // threads each push_front 10,000 distinct ints (pusher p pushes
    // p*10000 + k). At the same time, 2 remover threads loop
    // remove_if(is even) until the pushers finish. After joining everyone,
    // call remove_if(is even) once more, then collect every value with
    // for_each. Assert the result is exactly the 20,000 odd values, each
    // once. Catches: unlinking a node without holding the previous node's
    // lock, which loses or double-frees a node that's being pushed or
    // removed next to it (TSAN or ASan also flags it).
    threadsafe_list<int> list_instance;
    std::atomic<bool> test_1_start_flag(false);
    std::atomic<int> pushers_done(0);

    std::vector<std::thread> push_threads(4);
    for (size_t i = 0; i < push_threads.size(); ++i) {
        push_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            int finish = (i + 1) * 10000;
            int curr = finish - 10000;
            while (curr < finish) {
                list_instance.push_front(curr);
                curr++;
            }
            pushers_done.fetch_add(1);
        });
    }

    std::vector<std::thread> remover_threads(2);
    for (size_t i = 0; i < remover_threads.size(); ++i) {
        remover_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            while (pushers_done.load() < 4) {
                list_instance.remove_if([](int value) { return value % 2 == 0; });
            }
        });
    }

    // now we have all the tasks setup and blocking six threads
    // we now need to start the process using the flag:
    test_1_start_flag.store(true);
    // we need to then join all the pushers
    for (size_t i = 0; i < push_threads.size(); ++i) {
        push_threads[i].join();
    }
    // with the pushers joined, the removers will see pushers_done == 4 and
    // exit their loops on their own; join them
    for (size_t i = 0; i < remover_threads.size(); ++i) {
        remover_threads[i].join();
    }
    // the removers may have stopped mid-sweep, so run one final remove_if to
    // clear any even values pushed after their last pass
    list_instance.remove_if([](int value) { return value % 2 == 0; });

    // collect whatever is left in the list
    std::vector<int> remaining_values;
    list_instance.for_each([&](int value) { remaining_values.push_back(value); });
    std::cout << remaining_values.size() << std::endl;

    // TEST 1 ASSERT: exactly the 20,000 odd values in [0, 40000) remain,
    // each exactly once, and no even value survives.
    const int total_pushed = 40000;
    std::vector<int> value_counts(total_pushed, 0);
    for (int value : remaining_values) {
        assert(value >= 0 && value < total_pushed
            && "list contains a value no pusher pushed");
        assert(value % 2 == 1
            && "an even value survived remove_if");
        ++value_counts[value];
    }
    assert(remaining_values.size() == total_pushed / 2
        && "odd values were lost or duplicated in the list");
    size_t odd_count = 0;
    for (int value = 1; value < total_pushed; value += 2) {
        assert(value_counts[value] == 1
            && "an odd value is missing or appears more than once");
        ++odd_count;
    }
    assert(odd_count == total_pushed / 2);
    std::cout << "TEST 1 PASSED" << std::endl;

    // TEST # 2:
    // A slow node only holds up threads behind it. Push three items so the
    // list reads A, B, C from the head. Thread T runs for_each with a
    // function that sleeps 200 ms when it reaches C. The main thread waits
    // about 50 ms, then times push_front(D) followed by find_first_if(== A).
    // Assert both together return in well under 50 ms. Catches: one mutex
    // for the whole list, or holding the head lock for a whole traversal.
    threadsafe_list<char> list_instance_2;
    list_instance_2.push_front('C');
    list_instance_2.push_front('B');
    list_instance_2.push_front('A');
    std::thread T([&]() {
        list_instance_2.for_each([](const char& value) {
            if (value == 'C') {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
    });
    // give T time to get inside for_each and reach C; by now it holds only
    // C's node lock and has long released the head node's lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto head_ops_start = std::chrono::steady_clock::now();
    list_instance_2.push_front('D');
    std::shared_ptr<char> found
        = list_instance_2.find_first_if([](const char& value) { return value == 'A'; });
    const auto head_ops_elapsed = std::chrono::steady_clock::now() - head_ops_start;

    T.join();

    assert(found != nullptr && *found == 'A'
        && "find_first_if should have found A");
    // push_front only needs the head node's lock, and find_first_if(== A)
    // only walks head -> D -> A; neither touches C's lock, so the 200 ms
    // sleep at C must not stall them. With one mutex for the whole list (or
    // the head lock held across the whole traversal) both calls would block
    // for the ~150 ms remaining in T's sleep.
    assert(head_ops_elapsed < std::chrono::milliseconds(50)
        && "push_front/find_first_if blocked behind a slow node: a whole-list mutex or head lock held across the traversal");
    std::cout << "TEST 2 PASSED" << std::endl;

    return 0;
}
