#include "pending_requests.hpp"
#include <cassert>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Driver for the pending_requests build task (section 4.2.3).
//
// Requirements from the build spec -- see
// ch04_synchronizing_concurrent_operations/waiting-for-one-off-events-with-futures-notes.md
// for full detail:

int main()
{
    pending_requests<int, std::string> pending_requests_instance;
    // write the actual driver yourself, closed-book.
    //  - TEST#1: correctness under real concurrent registration, out-of-order
    //    fulfillment -- register 15-20+ requests from several concurrent caller
    //    threads, fulfill them out of registration order from a separate
    //    thread, verify each future's value matches its own id.

    std::vector<int> keys = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    std::vector<std::string> values = { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen" };
    std::map<int, std::string> test_tall_map;
    std::map<std::string, int> test_tall_map_reverse;
    for (size_t i = 0; i < keys.size(); ++i) {
        test_tall_map.emplace(keys[i], values[i]);
        test_tall_map_reverse.emplace(values[i], keys[i]);
    }
    // with the map prepped, im thinking we use async over in a loop and send it
    // a lambda function that registers all the keys in the object
    int num_threads = static_cast<int>(keys.size());
    std::vector<std::future<std::string>> string_futures_array(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        std::future<std::future<std::string>> resulting_async_future
            = std::async(std::launch::async,
                [&keys, &pending_requests_instance, i]() {
                    std::future<std::string> f
                        = pending_requests_instance.register_request(keys[i]);
                    return f;
                });
        string_futures_array[i] = resulting_async_future.get();
    }
    // now that we have those futures stored in the string_futures_array array above,
    // we can move on to the next step which is to call fulfill() with the ids and values

    for (size_t i = 0; i < keys.size(); ++i) {
        pending_requests_instance.fulfill(keys[i], values[i]);
    }
    // with the fullfill called - the promises ought to have been set and thus the futures
    // array ought to have all the strings loaded
    std::vector<std::string> number_string_results(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        number_string_results[i] = string_futures_array[i].get();
    }
    // we can ensure the order but we can ensure that all strings
    // against all the numbers got back so we can do a
    bool all_found = true;
    int count = 0;
    for (std::string num : number_string_results) {
        if (!test_tall_map_reverse.count(num)) {
            all_found = false;
            break;
        }
        count++;
    }
    assert((all_found && (count == static_cast<int>(values.size())))
        && "All of the strings are there in the map");

    //  - TEST#2: the abandoned-promise behavior is real -- register a request,
    //    never fulfill it, destroy the pending_requests object, verify .get()
    //    on that future actually throws rather than hanging.
    bool exception_throw_on_get = false;
    std::future<std::string> sacrificial_future;
    {

        pending_requests<int, std::string> pending_requests_instance_2;
        sacrificial_future = pending_requests_instance_2.register_request(1);
    }
    try {
        std::string res_2 = sacrificial_future.get();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        exception_throw_on_get = true;
    }
    assert(exception_throw_on_get
        && ".get() should have thrown the error as we destroyed promise holder object before setting value");
    std::cout << "All Tests Passed" << std::endl;
    return 0;
}
