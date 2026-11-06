#pragma once
#include "function_wrapper.hpp"
#include "join_threads.hpp"
#include "threadsafe_ptr_queue.hpp"
#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tp {

class work_stealing_thread_pool {

private:
    std::atomic<bool> done;
    threadsafe_ptr_queue<function_wrapper> pool_work_queue;
    std::vector<std::unique_ptr<work_stealing_queue>> queues;
    std::vector<std::thread> threads;
    join_threads joiner;

    static inline thread_local work_stealing_queue* local_work_queue;
    static inline thread_local int my_index;

    void worker_thread(int my_index_)
    {
        my_index = my_index_;
        local_work_queue = queues[my_index].get();
        while (!done.load()) {
            run_pending_task();
        }
    }

    bool pop_task_from_local_queue(function_wrapper& task)
    {
        return local_work_queue && local_work_queue->try_pop(task);
    }

    bool pop_task_from_pool_queue(function_wrapper& task)
    {
        return pool_work_queue.try_pop(task);
    }

    bool pop_task_from_other_thread_queue(function_wrapper& task)
    {
        for (size_t i = 0; i < queues.size(); ++i) {
            size_t index = (my_index + i + 1) % queues.size();
            if (queues[index]->try_steal(task)) {
                return true;
            }
        }
        return false;
    }

public:
    work_stealing_thread_pool()
        : done(false)
        , joiner(threads)
    {
        int thread_count = std::thread::hardware_concurrency();
        try {
            for (int i = 0; i < thread_count; ++i) {
                queues.push_back(std::unique_ptr<work_stealing_queue>(
                    new work_stealing_queue));
            }
            for (int i = 0; i < thread_count; ++i) {
                threads.push_back(std::thread(
                    &work_stealing_thread_pool::worker_thread, this, i));
            }
        } catch (const std::exception& e) {
            done.store(true);
            throw;
        }
    }

    ~work_stealing_thread_pool()
    {
        done.store(true);
    }

    template <typename FunctionType>
    std::future<typename std::result_of<FunctionType()>::type>
    submit(FunctionType f)
    {
        using result_type = typename std::result_of<FunctionType()>::type;
        std::packaged_task<result_type()> task(std::move(f));
        std::future<result_type> res(task.get_future());
        if (local_work_queue) {
            local_work_queue->push(std::move(task));
        } else {
            pool_work_queue.push(std::move(task));
        }
        return res;
    }

    void run_pending_task()
    {
        function_wrapper task;
        if (pop_task_from_local_queue(task)
            || pop_task_from_pool_queue(task)
            || pop_task_from_other_thread_queue(task)) {
            task();
        } else {
            std::this_thread::yield();
        }
    }
};

}