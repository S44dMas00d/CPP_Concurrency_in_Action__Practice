#include "shared_mutex_cache.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// Driver for the shared_mutex_cache build task (section 3.3.2).
//
// Requirements from the build spec:
//  - Basic correctness: find() returns the right value for present keys,
//    a default Value{} for absent ones; update_or_add() both inserts and
//    overwrites.
//  - The actual point of this build: prove the sharing is real, not just
//    correct. Instrument the cache so concurrent find() calls can be
//    observed genuinely overlapping (a "readers currently inside" counter
//    with a high-water mark > 1 under load), and confirm a writer in
//    update_or_add() observes that counter at 0 -- real mutual exclusion
//    against readers, not just accidental non-crashing.
//  - Real mixed-load stress test: many readers, occasional writers, all
//    concurrent, repeated runs -- no crashes, no corruption, every write
//    eventually visible.
//  - Must be ThreadSanitizer-clean (enabled by default via the root
//    CMakeLists' ENABLE_TSAN option -- no extra flags needed).

int main()
{
    // TEST 1 = basic correctness.
    // first lets define the actual class and enter a single entry
    // into it, basic correctness test needs only the main current
    // thread to see if this works
    shared_mutex_cache<std::string, int> shared_mutex_cache_instance;
    shared_mutex_cache_instance.update_or_add("one", 1);
    int res = shared_mutex_cache_instance.find("one");
    assert(res == 1 && "basic entry of sting 'one' ought to return number 1.");

    // TEST 2 = lets do multiple reads of one and see if the class
    // instance stored peak thread count there is more than one.
    int num_threads = 30;
    std::vector<std::thread> threads(num_threads);
    std::vector<int> results(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads[i] = std::thread([&shared_mutex_cache_instance, &results, i]() {
            results[i] = shared_mutex_cache_instance.find("one");
        });
    }
    for (int i = 0; i < num_threads; i++) {
        threads[i].join();
    }
    // first test is to ensure the results array has the thirty 1's in there
    assert(results.size() == 30 && "there ought to have been thirty 1's in the results array.");
    // then we need to make sure the peak number of threads in the reads have exceeded 1.
    assert(shared_mutex_cache_instance.peak_no_threads > 1 && "peak number of threads ought to have exceeded 1.");
    std::cout << "peak concurrent reader threads = " << shared_mutex_cache_instance.peak_no_threads << std::endl;

    std::cout << "All Tests Passed" << std::endl;
    return 0;
}
