#include "lazy_shared_resource.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// Driver for the lazy_shared_resource build task (section 3.3.1).
//
// Requirements from the build spec:
//  - Concurrent first-access stress test: 50+ threads all calling the
//    accessor for the very first time, as close to simultaneously as
//    possible. Confirm the resource's constructor ran exactly once (shared
//    construction counter).
//  - Every thread's returned handle must be fully initialized, not just
//    non-null -- confirm do_something() works cleanly from every thread,
//    including the ones that "lost" the race to construct it.
//  - Repeated calls well after construction must not re-trigger it --
//    construction counter stays at 1 under continued concurrent use.
//  - Must be ThreadSanitizer-clean (enabled by default via the root
//    CMakeLists' ENABLE_TSAN option -- no extra flags needed).

int main()
{
    int num_threads = 50;
    std::vector<expensive_resource*> pointers_to_single_instance(num_threads, nullptr);
    std::vector<std::thread> threads(num_threads);
    for (int i = 0; i < 50; i++) {
        threads[i] = std::thread(&expensive_resource::accessor, std::ref(pointers_to_single_instance[i]));
    }
    for (int i = 0; i < 50; i++) {
        threads[i].join();
    }
    // first we have to ensure that all the pointers in pointers_to_single_instance are not null and are the
    // same pointer:
    bool all_same_ptr = std::all_of(
        pointers_to_single_instance.begin(), pointers_to_single_instance.end(),
        [&](expensive_resource* x) { return (x == pointers_to_single_instance[0] && x != nullptr); });

    assert(all_same_ptr && "Should have have all same pointers or pointers to the same instance of expensive_resource.");

    // So up until this point, 50 threads have run the accessor function - and one of them would have
    // run the constructor - so that means that access_count ought to be 50 and initialization_count ought to
    // be 1;
    // now that we know all are the same pointer - we run tests on the first one:
    auto state = pointers_to_single_instance[0]->return_state();

    assert(state.initialization_count == 1 && "Initialization count ought to be 1");
    std::cout << "state.access_count = " << state.access_count << std::endl;
    assert(state.access_count == 50 && "access count count ought to be 50");

    // now run the nutation function using the pointer copies to the single instance you have in pointers_to_single_instance
    for (int i = 0; i < 50; i++) {
        pointers_to_single_instance[i]->do_something();
    }
    state = pointers_to_single_instance[0]->return_state();
    assert(state.mutation_count == 50 && "mutation count ought to be 50");
    std::cout << "All Tests Passed" << std::endl;
    return 0;
}
