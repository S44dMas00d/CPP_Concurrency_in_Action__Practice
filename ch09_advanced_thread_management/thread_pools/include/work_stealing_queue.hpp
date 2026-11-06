#pragma once
#include "function_wrapper.hpp"
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tp {

class work_stealing_queue {
private:
    std::deque<function_wrapper> de_queue;
    mutable std::mutex mut;

public:
    work_stealing_queue() {};
    work_stealing_queue(const work_stealing_queue& other) = delete;
    work_stealing_queue& operator=(const work_stealing_queue& other) = delete;

    void push(function_wrapper f)
    {
        std::lock_guard<std::mutex> lk(mut);
        de_queue.push_front(std::move(f));
    }

    bool try_pop(function_wrapper& res)
    {
        std::lock_guard<std::mutex> lk(mut);
        if (de_queue.empty()) {
            return false;
        }
        res = std::move(de_queue.front());
        de_queue.pop_front();
        return true;
    }

    bool try_steal(function_wrapper& res)
    {
        std::lock_guard<std::mutex> lk(mut);
        if (de_queue.empty()) {
            return false;
        }
        res = std::move(de_queue.back());
        de_queue.pop_back();
        return true;
    }

    bool empty()
    {
        std::lock_guard<std::mutex> lk(mut);
        return de_queue.empty();
    }
};

}