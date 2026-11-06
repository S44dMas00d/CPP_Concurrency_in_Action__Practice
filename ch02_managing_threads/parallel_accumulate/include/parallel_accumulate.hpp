#pragma once
#include <algorithm>
#include <iterator>
#include <numeric>
#include <thread>
#include <vector>

// parallel_accumulate — build task for section 2.4 (Choosing the number of
// threads at runtime).
//
// Interface, contract, and edge cases: see
// ch02_managing_threads/choosing-the-number-of-threads-at-runtime-notes.md.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// function yourself, per the framework's rule that Claude never writes the
// implementation.

template <typename Iterator, typename T>
struct accumulate_block {
    void operator()(Iterator first, Iterator last, T& result)
    {
        result = std::accumulate(first, last, result);
    }
};

template <typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init)
{
    // first we need to know the total number of elements in the supplied
    // data structure
    int const length = static_cast<int>(std::distance(first, last));
    // if this length is 0, we send back the init value:
    if (!length) {
        return init;
    }
    // now we need to define the minimum number of values a thread ought to
    // handle, id say it should be around 50:
    int const min_elements = 50;
    // now with that we need to also check the maximum number of thread we
    // are getting with the above minimum elements:
    int const max_threads = static_cast<int>(length / min_elements);
    // with this value we need to choose the number of threads we are actually
    // willing to spawn.
    int const num_hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
    int const num_spawn_threads = std::min((num_hardware_threads ? num_hardware_threads : 2), max_threads);
    // ok now at this point the number of threads been decided but we still need to then check the number
    // of elements we will need to give each thread based on this so:
    // since we are doing integer division - i expect some elements to be left over, we can get their direct
    // value however the tracking variables should also help us get them.
    // ok with the min number of threads, we now need to loop over the number of threads and in a one
    // pass loop with tracking variables with start and end points being updated, we need to instantiate
    // these threads
    if (num_spawn_threads == 0) {
        return std::accumulate(first, last, init);
    }
    // we also need a vector to store the results in
    Iterator start = first;
    Iterator end = first;
    int const num_elements_per_thread = static_cast<int>(length / num_spawn_threads);
    std::vector<T> results(num_spawn_threads);
    std::vector<std::thread> threads(num_spawn_threads - 1);
    for (int i = 0; i < (num_spawn_threads - 1); ++i) {
        end = start;
        std::advance(end, num_elements_per_thread);
        // move into container will be implicit here:
        threads[i] = std::thread(accumulate_block<Iterator, T>(), start, end, std::ref(results[i]));
        start = end;
    }
    // remaining elements should be taken care of by the caller thread
    // std::ref was needed above since with threads we need to be explicit with references.
    accumulate_block<Iterator, T>()(start, last, results[num_spawn_threads - 1]);
    // we now need to join the vector with the threads we have above, so we need to iterate over it once
    // more:
    for (std::size_t i = 0; i < std::size(threads); ++i) {
        threads[i].join();
    }
    // we now can assume that the results array will be populated with the block accumulates
    // need to call one more synchronous accumulate on that and we are good:
    return std::accumulate(results.begin(), results.end(), init);
}
