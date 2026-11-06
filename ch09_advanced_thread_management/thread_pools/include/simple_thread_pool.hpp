#pragma once
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

class simple_thread_pool {
private:
    std::atomic<bool> done;
    threadsafe_ptr_queue<std::function<void()>> work_queue;
    std::vector<std::thread> threads;
    join_threads joiner;

    void worker_thread()
    {
        while (!done.load()) {
            std::function<void()> task;
            if (work_queue.try_pop(task)) {
                task();
            } else {
                std::this_thread::yield();
            }
        }
    }

public:
    simple_thread_pool()
        : done(false)
        , joiner(threads)
    {
        int thread_count = std::thread::hardware_concurrency();
        try {
            for (int i = 0; i < thread_count; ++i) {
                threads.push_back(std::thread(&simple_thread_pool::worker_thread, this));
            }

        } catch (const std::exception& e) {
            done.store(true);
            throw;
        }
    }

    ~simple_thread_pool()
    {
        done.store(true);
    }

    template <typename FunctionType>
    void submit(FunctionType f)
    {
        work_queue.push(std::function<void()>(f));
    }
};

}