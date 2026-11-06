#include "task_runner.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

// Driver for the task_runner build task (section 4.2.2).
//
// Requirements from the build spec -- see
// ch04_synchronizing_concurrent_operations/waiting-for-one-off-events-with-futures-notes.md
// for full detail:

int main()
{
    // write the actual driver yourself, closed-book.
    //  - TEST#1: correctness + real contention on the queue itself -- post a
    //    meaningful number of tasks (20+) from several concurrent caller
    //    threads, wait on every returned future, verify the full result set.

    task_runner task_runner_instance;
    int num_tasks_1 = 24;
    std::vector<std::future<int>> futures_arr_1(num_tasks_1);
    std::vector<int> results_arr_1(num_tasks_1, 0);
    // post tasks from several concurrent caller threads (not sequentially
    // from main) so the queue's own mutex actually gets exercised under
    // contention. each producer thread owns a disjoint slice of indices,
    // so writing into futures_arr_1 concurrently is safe -- no reallocation,
    // no two threads touching the same element.
    int num_producers = 4;
    int chunk_size = num_tasks_1 / num_producers;
    std::vector<std::thread> producer_threads_1;
    for (int p = 0; p < num_producers; p++) {
        int start = p * chunk_size;
        int end = (p == num_producers - 1) ? num_tasks_1 : start + chunk_size;
        producer_threads_1.emplace_back([&task_runner_instance, &futures_arr_1, start, end]() {
            for (int i = start; i < end; i++) {
                futures_arr_1[i] = task_runner_instance.post_task([=]() {
                    return (i * 1) + (i * 2);
                });
            }
        });
    }
    for (auto& producer : producer_threads_1) {
        producer.join();
    }
    // next we call the .get() on the futures and check the values
    // amd we check using normal serial calculations if each
    // element in the results array matches the expected value
    bool all_values_came_as_expected = true;
    for (int i = 0; i < num_tasks_1; i++) {
        results_arr_1[i] = futures_arr_1[i].get();
        if (results_arr_1[i] != ((i * 1) + (i * 2))) {
            all_values_came_as_expected = false;
            break;
        }
    }

    assert(all_values_came_as_expected && "the values coming back from runner thread should match expected results.");

    //  - TEST#2: tasks actually run on the background thread, not the caller's.
    // now do this we can do the same thing we did above but with a
    // slightly different function.
    // we cant convert thread ids to ints to conform to the packaged
    // task, however we can pass a results array by reference that could
    // then be used to store the id

    int num_tasks = 10;
    std::vector<std::future<int>> futures_arr_2(num_tasks);
    std::vector<std::thread::id> thread_ids_arr_2(num_tasks);
    std::vector<int> results_arr_2(num_tasks, 0);
    //  we just use results arr 2 to hold 0s and return 1s to it to
    // indicate successful operations.
    for (int i = 0; i < num_tasks; i++) {
        futures_arr_2[i] = task_runner_instance.post_task(
            [&thread_ids_arr_2, i]() {
                thread_ids_arr_2[i] = std::this_thread::get_id();
                return 1;
            });
    }

    bool thread_ids_are_as_expected = true;
    for (int i = 0; i < num_tasks; i++) {
        results_arr_2[i] = futures_arr_2[i].get();
        if ((results_arr_2[i] != 1)
            || (thread_ids_arr_2[i] == std::this_thread::get_id())) {
            thread_ids_are_as_expected = false;
            break;
        }
    }

    assert(thread_ids_are_as_expected && "the thread ids from background ops ought to not match main thread's id");

    //  - TEST#3: the shutdown contract -- destructor returns promptly, the
    //  in-flight task finishes, queued-but-not-started tasks never run.

    // so i think we ought to do the same, i.e., make tasks and run them using
    // the same paradigm as before but halfway through .get calls in the second loop
    // we shutdown the runner and then see:
    std::vector<std::future<int>> futures_arr_3(num_tasks);
    std::vector<int> results_arr_3(num_tasks, 0);
    //  we just use results arr 2 to hold 0s and return 1s to it to
    // indicate successful operations.
    for (int i = 0; i < num_tasks; i++) {
        futures_arr_3[i] = task_runner_instance.post_task(
            [i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return 1;
            });
    }

    for (int i = 0; i < num_tasks; i++) {
        results_arr_3[i] = futures_arr_3[i].get();
        if (i == static_cast<int>(num_tasks / 2)) {
            task_runner_instance.shutdown();
            break;
        }
    }
    // in the watch window at this point im looking at 3 tasks stll in the deque
    // this means that those tasks had yet to be loaded on to the thread in the
    // task_runner_instance object.
    // in the results_arr_3 I see indexes 0 to 5, i.e., 6 elements with the
    // value 1 stored and indexes 6 to 9 with 0 stored and this means that
    // we were able to call get on 6 of the tasks which had ran on the thread
    // before the halfway point of tasks came and the runner object was shut down.
    // we still have 1 task left unaccounted for, i.e., not in the results and
    // not in the dequeue, well that one can be found in the futures_arr_3 array
    // at index 6 precisely as expected where if we start opening the future object
    // up, then under _M_state.get()._M_result.get() = 0x5555555866b0:
    // expression copy:
    // (((std::__shared_ptr<std::__future_base::_State_baseV2, (__gnu_cxx::_Lock_policy)2>::element_type *)0x555555586670)->_M_result).get();
    // where as the same expression for others under holds 0x0, as:
    // _M_state.get()._M_result.get() = 0x0
    // this basically means that we have one future we did not extract the value for
    // which managed to get executed in the thread before its shutdown and
    // the other futures after it did not get their associated tasks executed in
    // the bg thread.
    // give the in-flight task (the one already popped off the queue when
    // shutdown() was called) time to finish, then query the futures we
    // never called .get() on directly -- that's the real proof of the
    // shutdown contract, not inferring it from results_arr_3's untouched
    // zero-initialized slots. note: we can only wait_for() on futures_arr_3
    // indices from (num_tasks / 2 + 1) onward -- earlier ones already had
    // .get() called on them above, and querying an already-retrieved future
    // is invalid.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int first_abandoned_index = num_tasks / 2 + 1;
    int count_futures_ready_not_extracted = 0;
    int count_futures_timed_out = 0;
    for (int i = first_abandoned_index; i < num_tasks; i++) {
        if (futures_arr_3[i].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            count_futures_ready_not_extracted++;
        } else {
            count_futures_timed_out++;
        }
    }
    // exactly one task can have been mid-flight (already popped off the
    // queue) at the moment shutdown() was called -- the shutdown contract
    // lets it finish. everything still sitting in the queue behind it must
    // never become ready.
    assert(count_futures_ready_not_extracted <= 1 && "the one task already in flight when shutdown() was called should have completed");
    assert(count_futures_timed_out == (num_tasks - first_abandoned_index - 1) && "tasks still queued (never started) at shutdown must never become ready");
    std::cout
        << "All Tests Passed" << std::endl;
    return 0;
}
