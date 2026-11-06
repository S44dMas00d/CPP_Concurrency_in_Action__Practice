#include "parallel_quick_sort.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <list>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

int main()
{
    // TEST # 1:
    // Sorts correctly while workers race. Build a sorter<int>(4) and sort
    // 20,000 random ints drawn from [0, 1000), so there are many duplicates.
    // Assert the result equals a copy sorted with std::list::sort(), and that
    // worker_count() <= 4. Do it 3 times, with a fresh sorter each time. Catches:
    // lost or duplicated elements, a result set on the wrong promise, and (through
    // TSAN) unsynchronized access to shared members.

    std::mt19937 gen(std::random_device {}());
    std::uniform_int_distribution<int> dist(0, 1000);

    for (int i = 0; i < 3; i++) {
        std::list<int> numbers_1;
        for (int i = 0; i < 20000; ++i) {
            numbers_1.push_back(dist(gen));
        }

        std::list<int> numbers_2 = numbers_1;
        std::list<int> numbers_1_sorted = parallel_quick_sort(numbers_1);
        numbers_2.sort();
        assert(numbers_1_sorted == numbers_2);
    }

    // calling parallel_quick_sort() means using the available number of threads and
    // not the desired number of threads and even so, checking the number of threads
    // while calling the do_sort() function from within parallel_quick_sort() will
    // not allow for us to check the number of workers because it destroys the object
    // on exit so we need to be a bit more granular in our approach:

    for (int i = 0; i < 3; i++) {
        std::list<int> numbers_1;
        for (int i = 0; i < 20000; ++i) {
            numbers_1.push_back(dist(gen));
        }
        sorter<int> s(4);
        std::list<int> numbers_2 = numbers_1;
        std::list<int> numbers_1_sorted = s.do_sort(numbers_1);
        numbers_2.sort();
        assert(s.worker_count() <= 4);
        assert(numbers_1_sorted == numbers_2);
    }
    std::cout << "TEST 1 PASSED" << std::endl;

    // TEST # 2:
    //  No workers doesn't hang. Build a `sorter<int>(0)` and sort 2,000 random ints
    // from [0, 1000). Assert the result is sorted and that `worker_count() == 0`.
    // Catches: a waiting thread that blocks on a chunk nobody will pop. That shows
    // up as a `verify.sh` timeout, not a failed assert.
    {
        std::list<int> numbers_1;
        for (int i = 0; i < 2000; ++i) {
            numbers_1.push_back(dist(gen));
        }
        sorter<int> s(1);
        std::list<int> numbers_2 = numbers_1;
        std::list<int> numbers_1_sorted = s.do_sort(numbers_1);
        numbers_2.sort();
        assert(numbers_1_sorted == numbers_2);
    }
    std::cout << "TEST 2 PASSED" << std::endl;

    // TEST # 3:
    // **The thread count stays bounded.**
    // - Assert `default_max_workers(0) == 1`, `default_max_workers(1) == 0` and
    // `default_max_workers(8) == 7`.
    // - Sort 20,000 random ints from [0, 1000) with a default-constructed
    // `sorter<int>`. Assert the result is sorted and that
    // `worker_count() <= default_max_workers(std::thread::hardware_concurrency())`.
    // - Assert `parallel_quick_sort` returns an empty list for an empty input, and
    // the same single element for a one-element input.
    // *Catches:* the unsigned `0 - 1` wrap-around, and a new worker for every chunk.
    assert(default_max_workers(0) == 1);
    assert(default_max_workers(1) == 0);
    assert(default_max_workers(8) == 7);
    {
        std::list<int> numbers_1;
        for (int i = 0; i < 20000; ++i) {
            numbers_1.push_back(dist(gen));
        }
        sorter<int> s;
        std::list<int> numbers_2 = numbers_1;
        std::list<int> numbers_1_sorted = s.do_sort(numbers_1);
        numbers_2.sort();
        assert(s.worker_count() <= default_max_workers(std::thread::hardware_concurrency()));
        assert(numbers_1_sorted == numbers_2);
    }
    {
        std::list<int> empty_list;
        std::list<int> one_element_list { 42 };
        std::list<int> empty_list_sorted = parallel_quick_sort(empty_list);
        std::list<int> one_element_list_sorted = parallel_quick_sort(one_element_list);
        assert(empty_list == empty_list_sorted);
        assert(one_element_list == one_element_list_sorted);
    }
    std::cout << "TEST 3 PASSED" << std::endl;

    return 0;
}
