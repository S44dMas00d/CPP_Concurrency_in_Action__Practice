#pragma once
#include <exception>
#include <memory>
#include <mutex>
#include <stack>
#include <thread>
#include <vector>

class join_threads {
private:
    std::vector<std::thread>& threads;

public:
    join_threads(std::vector<std::thread>& threads_)
        : threads(threads_)
    {
    }
    ~join_threads()
    {
        for (size_t i = 0; i < threads.size(); ++i) {
            if (threads[i].joinable()) {
                threads[i].join();
            }
        }
    }
};
