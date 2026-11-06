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
Iterator async_find_impl(Iterator first, Iterator last,
    MatchType match, std::atomic<bool>& done)
{
    try {
        int length = std::distance(first, last);
        int min_per_thread = 25;

        if (length < (2 * min_per_thread)) {
            for (; (first != last) && (!done.load()); ++first) {
                if (*first == match) {
                    done.store(true);
                    return first;
                }
            }
            return last;
        } else {
            Iterator mid_point = first + (length / 2);
            std::future<Iterator> async_right_half_result
                = std::async(&async_find_impl<Iterator, MatchType>,
                    mid_point, last, match, std::ref(done));
            Iterator direct_left_half_result
                = async_find_impl(first, mid_point, match, done);

            return (direct_left_half_result == mid_point
                    ? async_right_half_result.get()
                    : direct_left_half_result);
        }

    } catch (const std::exception& e) {
        done.store(true);
        throw e;
    }
}

template <typename Iterator, typename MatchType>
Iterator async_find(Iterator first, Iterator last, MatchType match)
{
    std::atomic<bool> done(false);
    return async_find_impl(first, last, match, done);
}
