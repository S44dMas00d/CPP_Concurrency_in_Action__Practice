#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

template <typename T>
class threadsafe_ptr_queue {
private:
    std::condition_variable data_cond;
    mutable std::mutex mut;
    std::queue<std::shared_ptr<T>> data_queue;

public:
    threadsafe_ptr_queue() {};
    threadsafe_ptr_queue(const threadsafe_ptr_queue& other) = delete;
    threadsafe_ptr_queue& operator=(const threadsafe_ptr_queue& other) = delete;

    void push(T new_value)
    {
        std::shared_ptr<T> data_ptr(std::make_shared<T>(std::move(new_value)));
        std::lock_guard<std::mutex> lk(mut);
        data_queue.push(data_ptr);
        data_cond.notify_one();
    }

    bool try_pop(T& value)
    {
        std::lock_guard<std::mutex> lk(mut);
        if (data_queue.empty()) {
            return false;
        }
        value = std::move(*(data_queue.front()));
        data_queue.pop();
        return true;
    }

    std::shared_ptr<T> try_pop()
    {
        std::lock_guard<std::mutex> lk(mut);
        if (data_queue.empty()) {
            return nullptr;
        }
        std::shared_ptr<T> data_ptr = std::move(data_queue.front());
        data_queue.pop();
        return data_ptr;
    }

    void wait_and_pop(T& value)
    {
        std::unique_lock<std::mutex> ulk(mut);
        data_cond.wait(ulk, [this]() { return !data_queue.empty(); });
        value = std::move(*(data_queue.front()));
        data_queue.pop();
        return;
    }

    std::shared_ptr<T> wait_and_pop()
    {
        std::unique_lock<std::mutex> ulk(mut);
        data_cond.wait(ulk, [this]() { return !data_queue.empty(); });
        std::shared_ptr<T> data_ptr = std::move(data_queue.front());
        data_queue.pop();
        return data_ptr;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lk(mut);
        return data_queue.empty();
    }
};
