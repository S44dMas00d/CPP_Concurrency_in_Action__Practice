#pragma once
#include "thread_pool.hpp"
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace tp {

template <typename Iterator, typename T>
struct accumulate_block {
    T operator()(Iterator first, Iterator last)
    {
        return std::accumulate(first, last, T());
    }
};

template <typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init)
{
    int length = std::distance(first, last);
    if (!length) {
        return init;
    }
    int block_size = 50;
    int num_blocks = (length + block_size - 1) / block_size;

    std::vector<std::future<T>> futures(num_blocks - 1);
    thread_pool pool;

    Iterator block_start = first;
    for (int i = 0; i < (num_blocks - 1); ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        futures[i] = pool.submit(
            std::bind(accumulate_block<Iterator, T>(), block_start, block_end));
        block_start = block_end;
    }
    T last_result = accumulate_block<Iterator, T>()(block_start, last);

    T result = init;
    for (size_t i = 0; i < futures.size(); i++) {
        result += futures[i].get();
    }
    result += last_result;
    return result;
};

}