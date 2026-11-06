#pragma once
#include "join_threads.hpp"
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <stack>
#include <thread>
#include <vector>

template <typename Iterator, typename T>
struct accumulate_block {
    T operator()(Iterator first, Iterator last)
    {
        return std::accumulate(first, last, T());
    }
};

template <typename Iterator, typename T>
T safe_parallel_accumulate(Iterator first, Iterator last, T init)
{
    int length = std::distance(first, last);
    if (!length) {
        return init;
    }
    int min_per_thread = 25;
    int max_threads = (length + min_per_thread - 1) / min_per_thread;
    int hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
    int num_threads = std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    std::vector<std::thread> threads(num_threads - 1);
    std::vector<std::future<T>> futures(num_threads - 1);
    join_threads joiner(threads);
    int block_size = length / num_threads;

    Iterator block_start = first;
    for (int i = 0; i < (num_threads - 1); ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        std::packaged_task<T(Iterator, Iterator)> task {
            accumulate_block<Iterator, T>()
        };
        futures[i] = task.get_future();
        threads[i] = std::thread(std::move(task), block_start, block_end);
        block_start = block_end;
    }

    T last_res = accumulate_block<Iterator, T>()(block_start, last);

    T result = init;
    for (int i = 0; i < (num_threads - 1); ++i) {
        result += futures[i].get();
    }
    result += last_res;
    return result;
}