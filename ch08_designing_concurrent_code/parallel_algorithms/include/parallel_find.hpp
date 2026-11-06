#pragma once
#include "join_threads.hpp"
#include <algorithm>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <stack>
#include <thread>
#include <vector>

template <typename Iterator, typename MatchType>
Iterator parallel_find(Iterator first, Iterator last, MatchType match)
{
    struct find_element {
        void operator()(Iterator begin, Iterator end,
            MatchType match,
            std::promise<Iterator>* result,
            std::atomic<bool>* done_flag)
        {
            try {
                for (; (begin != end) && (!done_flag->load()); ++begin) {
                    if (*begin == match) {
                        result->set_value(begin);
                        done_flag->store(true);
                        return;
                    }
                }
            } catch (const std::exception& e) {
                result->set_exception(std::current_exception());
                done_flag->store(true);
            }
        }
    };

    int length = std::distance(first, last);
    if (!length) {
        return last;
    }
    int min_per_thread = 25;
    int max_threads = (length + min_per_thread - 1) / min_per_thread;
    int hardware_threads = std::thread::hardware_concurrency();
    int num_threads = std::min(hardware_threads != 0 ? hardware_threads : 2, max_threads);
    int block_size = length / num_threads;

    std::promise<Iterator> result;
    std::atomic<bool> done_flag(false);
    std::vector<std::thread> threads(num_threads - 1);
    {
        join_threads joiner(threads);
        Iterator block_start = first;
        for (int i = 0; i < (num_threads - 1); ++i) {
            Iterator block_end = block_start;
            std::advance(block_end, block_size);
            threads[i] = std::thread(find_element(), block_start,
                block_end, match, &result, &done_flag);
            block_start = block_end;
        }
        find_element()(block_start, last, match, &result, &done_flag);
    }
    if (!done_flag.load()) {
        return last;
    }
    return result.get_future().get();
}
