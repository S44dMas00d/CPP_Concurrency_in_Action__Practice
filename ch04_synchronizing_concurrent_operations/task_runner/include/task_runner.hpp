#pragma once
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

// task_runner — build task for section 4.2.2 (Associating a task with a
// future / std::packaged_task).
//
// Interface, thread-safety contract, shutdown contract, and edge cases: see
// ch04_synchronizing_concurrent_operations/waiting-for-one-off-events-with-futures-notes.md
// for the full build spec.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// class yourself, per the framework's rule that Claude never writes the
// implementation. Remember: anything you define outside the class body at
// namespace scope in this header needs `inline` (ODR) — member functions
// defined in-class don't.

class task_runner {
private:
    bool to_shutdown;
    std::mutex mut;
    std::condition_variable data_cond;
    std::deque<std::packaged_task<int()>> task_dequeue;
    std::thread runner_thread;

public:
    task_runner()
        : to_shutdown(false)
        , mut()
        , data_cond()
        , task_dequeue()
        , runner_thread(&task_runner::run_task, this)

    {
    }
    ~task_runner()
    {
        shutdown();
        if (runner_thread.joinable()) {
            runner_thread.join();
        }
    };
    // copy constructor and assignment operator deleted
    task_runner(const task_runner& other) = delete;
    task_runner& operator=(const task_runner& other) = delete;

    // move constructor and assignment operator also deleted (FOR NOW)
    task_runner(const task_runner&& other) = delete;
    task_runner& operator=(const task_runner&& other) = delete;

    // Wraps f in a std::packaged_task<int()>, queues it for a worker
    // thread, and returns its future. Accepts any no-argument callable
    // (bind/lambda-capture your way around needing arguments — this is
    // int() only, same scope as Listing 4.9's example, not a generalized
    // ResultType<T> version).
    template <typename Func>
    std::future<int> post_task(Func f)
    {
        std::future<int> task_future;
        {
            std::lock_guard<std::mutex> lk(mut);
            std::packaged_task<int()> task(f);
            task_future = task.get_future();
            task_dequeue.push_back(std::move(task));
        }
        data_cond.notify_one();
        return task_future;
    }

    // since this class is the task runner and we just push to its internal
    // queue and leave the handling of tasks and orders to it, we need to
    // make the consumer function for the packaged_task as well and oeprator
    // overloading () looks to be great for this:
    void run_task()
    {
        std::unique_lock<std::mutex> u_lk(mut);
        while (true) {
            // we need the lock and lets make it a unique lock and we can
            // unlock before running the task.
            // we need to make the condition_variable wait, we block and sleep here
            // let others have the mutex and sleep.
            data_cond.wait(u_lk, [this]() { return to_shutdown || !task_dequeue.empty(); });
            // from here we have the lock until we unlock or leave the scope. we
            // need to handle shutdown and normal operation, eitherway we have RAII on
            // mut so its fine.
            if (to_shutdown) {
                return;
                // basically we return and dont care if the deque isnt empty.
                // such was the contract for shutdown
            }
            // normal operation would have us moving the callable into packaged_task
            // here and execute it but after the unlock
            std::packaged_task<int()> task = std::move(task_dequeue.front());
            task_dequeue.pop_front();
            // Release the mutex before executing the task.
            // The mutex only protects access to the task queue; holding it while
            // running the task would block other threads from adding/removing tasks
            // and could deadlock if the task itself tries to access the queue.
            // Re-lock before the next wait() so the predicate check and
            // wait/sleep transition remain synchronized with producers.
            u_lk.unlock();

            // Execute task without holding the mutex.
            // We must re-lock before the next iteration because
            // condition_variable::wait() requires the lock to be owned.
            task();

            // Re-lock before wait(): wait() must check the predicate while holding
            // the mutex. This synchronizes the predicate check with producers,
            // preventing the "check -> producer notifies -> worker goes to sleep"
            // missed-notification race.
            u_lk.lock();
        }
    };

    std::thread::id get_run_task_thread_id()
    {
        return std::this_thread::get_id();
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lk(mut);
            to_shutdown = true;
        }
        data_cond.notify_one();
        return;
    }
};
