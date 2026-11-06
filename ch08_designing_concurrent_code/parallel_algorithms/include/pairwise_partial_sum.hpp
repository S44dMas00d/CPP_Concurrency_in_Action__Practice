#pragma once
#include "barrier.hpp"
#include "join_threads.hpp"
#include <algorithm>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

template <typename Iterator>
void pairwise_partial_sum(Iterator first, Iterator last)
{

    using value_type = typename Iterator::value_type;

    struct process_element {
        void operator()(Iterator first, Iterator last,
            std::vector<value_type>& buffer, int i, barrier& b)
        {
            value_type& ith_element = *(first + i);
            bool update_source = false;
            for (int step = 0, stride = 1;
                 stride <= i;
                 ++step, stride *= 2) {
                value_type& source = (step % 2) ? buffer[i] : ith_element;
                value_type& dest = (step % 2) ? ith_element : buffer[i];
                value_type& addend = (step % 2)
                    ? buffer[i - stride]
                    : *(first + i - stride);
                dest = source + addend;
                update_source = !(step % 2);
                b.wait();
            }
            if (update_source) {
                ith_element = buffer[i];
            } else {
                buffer[i] = ith_element;
            }
            b.done_waiting();
        }
    };

    int const length = std::distance(first, last);
    if (length <= 1) {
        return;
    }
    std::vector<value_type> buffer(length);
    barrier b(length);
    std::vector<std::thread> threads(length - 1);
    join_threads joiner(threads);
    Iterator block_start = first;
    for (int i = 0; i < (length - 1); ++i) {
        threads[i] = std::thread(process_element(), first, last,
            std::ref(buffer), i, std::ref(b));
    }
    process_element()(first, last, buffer, length - 1, b);
}
