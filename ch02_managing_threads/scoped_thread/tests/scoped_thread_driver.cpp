#include "scoped_thread.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>
// Driver for the scoped_thread build task (section 2.3).
//
// Not a concurrency stress test (same reasoning as thread_guard) — this is
// about ownership and lifetime correctness, not data races. Requirements
// from the build spec:
//  - Constructing from an already non-joinable std::thread throws.
//  - Normal-path: owned thread is joined on destruction.
//  - std::vector<scoped_thread> — does it compile as-is? If not, work out
//    why (rule-of-5 / implicit move suppression) and what fixes it.

struct functor {
    int& i;
    functor(int& i_)
        : i(i_)
    {
    }

    void operator()()
    {
        for (int j = 0; j < 10; j++) {
            std::cout << "functor 1 - print num: " << i << std::endl;
            i++;
        }
    }
};
struct functor2 {
    int i;
    functor2(int& i_)
        : i(i_)
    {
    }

    void operator()()
    {
        std::cout << "functor 2 - print single num: " << i << std::endl;
    }
};

// the scoped thread class must not be copyable as that could allow for it
// to outlast the thread caller scope or even outlive the thread it references
// and one of it copies joins and kills while some other copy survives.
static_assert(!std::is_copy_constructible_v<scoped_thread>,
    "scoped_thread must not be copy constructable");
static_assert(!std::is_copy_assignable_v<scoped_thread>,
    "scoped_thread must not be copy assignable");

void std_vector_container_test()
{
    std::vector<scoped_thread> scoped_threads;
    // try {
    //     for (int i = 0; i < 20; i++) {
    //         scoped_threads.push_back(scoped_thread(std::thread(functor2(i))));
    //     }
    // } catch (const std::exception& e) {
    //     std::cerr << e.what() << '\n';
    // }
    std::cout << "Cannot compile this - check the comment for explainer\n";

    // ANSWER FOR THIS: scoped_thread has a defined / custom destructor
    // this ive read - suppresses the implicit move constructor.
    // we also have deleted copy and assign constructors and this means
    // vector has no means for reallocating elements on reallocation with
    // resizing.
    // A solution doesnt come to mind - my knowledge is sadly limited here.
}
void non_joinable_test()
{
    int local_num = 0;
    functor ff2(local_num);
    std::thread t2(ff2);
    t2.join();
    assert(!t2.joinable() && "t2 should no longer be joinable after we joined it");
    try {
        scoped_thread sc2 = scoped_thread(std::move(t2));
    } catch (const std::exception& e) {
        std::cout << "non joinable test PASS - an already joined thread cant be given to scoped_thread class" << std::endl;
        return;
    }
    std::cout << "non joinable test FAIL - an already joined thread somehow made it into scoped_thread class" << std::endl;
}
void normal_test()
{
    int local_num = 0;
    functor ff1(local_num);
    std::thread t1(ff1);
    {
        scoped_thread stc1(std::move(t1));
    }
    assert(!t1.joinable() && "t1 should no longer be joinable after scoped_thread took ownership and joined it");
    assert(local_num == 10 && "join() should have run the thread to completion");
    std::cout << "normal_path_test passed\n";
}

int main()
{
    normal_test();
    non_joinable_test();
    std_vector_container_test();
    return 0;
}
