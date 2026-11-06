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

template <typename Iterator, typename Func>
void async_for_each(Iterator first, Iterator last, Func f)
{
    int length = std::distance(first, last);
    if (!length) {
        return;
    }
    int min_per_thread = 25;
    if (length < (2 * min_per_thread)) {
        std::for_each(first, last, f);
    } else {
        Iterator mid_point = first + length / 2;
        std::future<void> first_half_future = std::async(
            &async_for_each<Iterator, Func>,
            first, mid_point, f);
        async_for_each(mid_point, last, f);
        first_half_future.get();
    }
}
