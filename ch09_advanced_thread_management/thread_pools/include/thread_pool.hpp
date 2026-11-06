#pragma once
#include "function_wrapper.hpp"
#include "join_threads.hpp"
#include "threadsafe_ptr_queue.hpp"
#include <atomic>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tp {

class thread_pool {
private:
    std::atomic<bool> done;
    threadsafe_ptr_queue<function_wrapper> work_queue;
    std::vector<std::thread> threads;
    join_threads joiner;

    void worker_thread()
    {
        while (!done.load()) {
            run_pending_task();
        }
    }

public:
    thread_pool()
        : done(false)
        , joiner(threads)
    {
        int thread_count = std::thread::hardware_concurrency();
        try {
            for (int i = 0; i < thread_count; ++i) {
                threads.push_back(std::thread(&thread_pool::worker_thread, this));
            }
        } catch (const std::exception& e) {
            done.store(true);
            throw;
        }
    }

    ~thread_pool()
    {
        done.store(true);
    }

    void run_pending_task()
    {
        function_wrapper task;
        if (work_queue.try_pop(task)) {
            task();
        } else {
            std::this_thread::yield();
        }
    }

    template <typename FunctionType>
    std::future<typename std::result_of<FunctionType()>::type>
    submit(FunctionType f)
    {
        using result_type = typename std::result_of<FunctionType()>::type;
        std::packaged_task<result_type()> task(std::move(f));
        std::future<result_type> res(task.get_future());
        work_queue.push(std::move(task));
        return res;
    }
};

}
