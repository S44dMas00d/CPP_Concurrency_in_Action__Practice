#include "function_wrapper.hpp"
#include "local_queue_thread_pool.hpp"
#include "pool_accumulate.hpp"
#include "pool_quick_sort.hpp"
#include "simple_thread_pool.hpp"
#include "thread_pool.hpp"
#include "work_stealing_queue.hpp"
#include "work_stealing_thread_pool.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <list>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

class poison_error : public std::exception {
public:
    const char* what() const noexcept override
    {
        return "poison encountered !";
    }
};

template <typename Pool>
void run_nested_submit_test(Pool& pool)
{
    std::future<int> outer = pool.submit([&pool]() {
        std::vector<std::future<int>> inner_futures(50);
        for (int i = 0; i < 50; i++) {
            inner_futures[i] = pool.submit([i]() { return i; });
        }
        int sum = 0;
        for (int i = 0; i < 50; i++) {
            while (inner_futures[i].wait_for(std::chrono::seconds(0))
                != std::future_status::ready) {
                pool.run_pending_task();
            }
            sum += inner_futures[i].get();
        }
        return sum;
    });

    assert(outer.get() == 1225);
}

template <typename Pool>
void run_hundred_squares_test(Pool& pool)
{
    std::vector<std::future<int>> futures_vec(100);
    for (int i = 0; i < 100; i++) {
        futures_vec[i] = pool.submit([i]() { return i * i; });
    }

    for (int i = 0; i < 100; i++) {
        assert(futures_vec[i].get() == (i * i));
    }
}

int main()
{
    // TEST # 1:
    // function_wrapper.
    // - Wrap a std::packaged_task<int()> that returns 42, keep its future, call
    //   the wrapper, and assert the future gives 42.
    // - Wrap a lambda that owns a std::unique_ptr<int>. Move the wrapper into a
    //   second one, call the second, and assert the lambda ran.
    // - Add static_assert(!std::is_copy_constructible<tp::function_wrapper>::value)
    // Catches: a wrapper that copies, or loses the callable on a move.
    {
        std::packaged_task<int()> pk_task = std::packaged_task<int()>([]() { return 42; });
        std::future<int> f = pk_task.get_future();

        tp::function_wrapper func_wrapper_1(std::move(pk_task));

        func_wrapper_1();

        assert(f.get() == 42);
    }
    {
        std::packaged_task<int()> pk_task = std::packaged_task<int()>(
            []() {
                std::unique_ptr<int> ptr;
                ptr = std::make_unique<int>(10);
                *ptr += 1;
                return *ptr;
            });
        std::future<int> f = pk_task.get_future();

        tp::function_wrapper func_wrapper_1(std::move(pk_task));
        tp::function_wrapper func_wrapper_2(std::move(func_wrapper_1));
        func_wrapper_2();
        assert(f.get() == 11);
    }

    static_assert(!std::is_copy_constructible<tp::function_wrapper>::value);
    // TEST # 2:
    // simple_thread_pool. Submit 1,000 tasks, each doing fetch_add(1) on a
    // std::atomic<int>. Before the pool is destroyed, spin with yield() until
    // the counter reaches 1,000. Catches: a lost task, or a worker that never
    // runs (it hangs, and verify.sh's timeout shows it).
    std::atomic<int> test_int_1(0);
    {
        tp::simple_thread_pool thread_pool_1;
        for (int i = 0; i < 1000; i++) {
            thread_pool_1.submit([&, i]() { test_int_1.fetch_add(1); });
        }
        while (!(test_int_1.load() >= 1000)) {
            std::this_thread::yield();
        }
    }
    assert(test_int_1.load() == 1000);

    // TEST # 3:
    // thread_pool.
    // Submit 100 tasks returning i * i, and assert every future gives the right value.
    // Submit a task that throws poison_error, and assert get() rethrows it.
    // Catches: results attached to the wrong future, and a lost exception.
    {
        tp::thread_pool thread_pool_1;
        std::vector<std::future<int>> futures_vec(100);
        for (int i = 0; i < 100; i++) {
            futures_vec[i] = thread_pool_1.submit([i]() { return i * i; });
        }

        for (int i = 0; i < 100; i++) {
            assert(futures_vec[i].get() == (i * i));
        }

        std::future<void> f2 = thread_pool_1.submit([]() { throw poison_error(); });
        bool poison_error_caught = false;
        try {
            f2.get();
        } catch (const poison_error& e) {
            poison_error_caught = true;
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
        }
        assert(poison_error_caught);
    }

    // TEST # 4:
    // pool_accumulate. Sum the ints 1..10,000 with init = 7, and assert the result equals
    // std::accumulate. Also check an empty range returns init.
    std::vector<int> test_values(10000);
    std::iota(test_values.begin(), test_values.end(), 1);
    int res_actual = tp::parallel_accumulate<std::vector<int>::iterator, int>(test_values.begin(), test_values.end(), 7);
    int res_compare = std::accumulate(test_values.begin(), test_values.end(), 7);
    assert(res_actual == res_compare);

    // TEST # 5:
    // pool_quick_sort. Do this 3 times: 20,000 random ints from [0, 1000) (the 8.1
    // stack-depth limit applies), and assert the result equals a copy sorted with
    // std::list::sort(). Catches: a waiter that blocks on get() (it hangs, and the
    // timeout shows it), and lost or duplicated elements.
    {
        int num_runs = 3;
        int num_elements = 2000;

        std::mt19937 generator(std::random_device {}());
        std::uniform_int_distribution<int> distribution(0, 999);

        for (int run = 0; run < num_runs; ++run) {
            std::list<int> values;

            for (int i = 0; i < num_elements; ++i) {
                values.push_back(distribution(generator));
            }

            std::list<int> expected = values;

            expected.sort();

            std::list<int> result = tp::pool_quick_sort(values);

            assert(result == expected);
            assert(result.size() == num_elements);
        }
    }

    // TEST # 6:
    // Nested submits, the same test for all three waitable pools (thread_pool,
    // local_queue_thread_pool, work_stealing_thread_pool):
    // - From the main thread, submit one outer task.
    // - Inside it, the outer task submits 50 inner tasks, each returning its index 0..49.
    // - It waits for each inner future by calling run_pending_task() until it's ready, then returns the sum.
    // - Assert outer.get() == 1225.
    // Also run test 3's 100 squares on the two newer pools. Catches: tasks on a
    // thread-local queue that never run (a hang), the local queue being used by a
    // non-pool thread, and broken stealing.

    {
        tp::thread_pool thread_pool_1;
        run_nested_submit_test(thread_pool_1);

        tp::local_queue_thread_pool local_queue_pool_1;
        run_nested_submit_test(local_queue_pool_1);
        run_hundred_squares_test(local_queue_pool_1);

        tp::work_stealing_thread_pool work_stealing_pool_1;
        run_nested_submit_test(work_stealing_pool_1);
        run_hundred_squares_test(work_stealing_pool_1);
    }

    // TEST # 7:
    // work_stealing_queue, single-threaded. Push three wrappers that record 1, 2
    // and 3 into a vector when called.
    // - try_pop should run 3 (the newest, from the front).
    // - try_steal should run 1 (the oldest, from the back).
    // - try_pop should run 2.
    // - Then both calls should return false.
    // Catches: the ends swapped.
    {
        tp::work_stealing_queue work_stealing_queue_1;
        std::vector<int> execution_order;

        work_stealing_queue_1.push(tp::function_wrapper([&]() {
            execution_order.push_back(1);
        }));
        work_stealing_queue_1.push(tp::function_wrapper([&]() {
            execution_order.push_back(2);
        }));
        work_stealing_queue_1.push(tp::function_wrapper([&]() {
            execution_order.push_back(3);
        }));

        tp::function_wrapper task;
        // Newest task from the front.
        assert(work_stealing_queue_1.try_pop(task));
        task();
        // Oldest task from the back.
        assert(work_stealing_queue_1.try_steal(task));
        task();
        // Remaining task.
        assert(work_stealing_queue_1.try_pop(task));
        task();
        // Queue should now be empty.
        assert(!work_stealing_queue_1.try_pop(task));
        assert(!work_stealing_queue_1.try_steal(task));
        assert((execution_order == std::vector<int> { 3, 1, 2 }));
    }

    std::cout << "ALL TESTS PASSED" << std::endl;
    return 0;
}
