#include "thread_guard.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <type_traits>

// Driver for the thread_guard build task (section 2.1).
//
// This one isn't a concurrency stress test — thread_guard is never accessed
// from multiple threads, so TSAN isn't the relevant signal here. What needs
// exercising instead:
//  - Normal-path destruction joins the guarded thread correctly.
//  - An exception thrown mid-scope still results in a join before the
//    exception propagates out.
//  - static_assert (or equivalent) confirms thread_guard is not
//    copy-constructible / copy-assignable.
//  - Manually joining or detaching before the guard's destructor runs
//    doesn't crash (joinable() check working).

struct functor {

    int& i;

    functor(int& i_)
        : i(i_)
    {
    }

    void operator()()
    {
        for (int j = 0; j < 100; j++) {
            std::cout << "printnum : " << i << std::endl;
            i++;
        }
    }
};

// thread_guard must not be copyable (contract: copying risks a double-join
// or a guard outliving the thread it references).
static_assert(!std::is_copy_constructible_v<thread_guard>,
    "thread_guard must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<thread_guard>,
    "thread_guard must not be copy-assignable");

// Original scenario: normal-path destruction joins the guarded thread
// correctly, before the guarded reference's owner goes away.
void normal_path_test()
{
    int local_var = 3;
    functor ff1(local_var);
    std::thread t1(ff1);
    {
        thread_guard tg(t1);
    } // tg destructs here -> should join t1 before this scope exits

    assert(!t1.joinable() && "t1 should no longer be joinable after thread_guard joined it");
    assert(local_var == 3 + 100 && "join() should have run the thread to completion");
    std::cout << "normal_path_test passed\n";
}

// An exception thrown mid-scope still results in a join before the
// exception propagates out — the actual reason thread_guard exists over
// listing 2.2's try/catch version.
void exception_path_test()
{
    int local_var = 0;
    functor ff1(local_var);
    std::thread t1(ff1);
    try {
        thread_guard tg(t1);
        throw std::runtime_error("boom");
    } catch (std::runtime_error const&) {
        // tg was destroyed during unwinding, before we got here — it must
        // have joined t1 already.
        assert(!t1.joinable() && "t1 should have been joined during exception unwinding");
        std::cout << "exception_path_test passed\n";
        return;
    }
    std::cout << "exception_path_test FAILED: exception was not caught\n";
}

// Manually joining before the guard's destructor runs must not crash —
// the destructor's joinable() check has to make this safe.
void manual_join_before_guard_destructs_test()
{
    int local_var = 0;
    functor ff1(local_var);
    std::thread t1(ff1);
    {
        thread_guard tg(t1);
        t1.join(); // manual join, ahead of tg's destructor
        assert(!t1.joinable());
    } // tg destructs here; joinable() is false, so it must do nothing

    std::cout << "manual_join_before_guard_destructs_test passed\n";
}

int main()
{
    normal_path_test();
    exception_path_test();
    manual_join_before_guard_destructs_test();
    std::cout << "all thread_guard tests passed\n";
    return 0;
}
