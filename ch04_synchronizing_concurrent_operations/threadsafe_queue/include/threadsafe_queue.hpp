#pragma once
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
// threadsafe_queue — build task for section 4.1.2 (Building a thread-safe
// queue with condition variables).
//
// Interface, thread-safety contract, and edge cases: see
// ch04_synchronizing_concurrent_operations/waiting-for-an-event-or-condition-notes.md
// for the full build spec.
//
// Deliberately empty. This is the closed-book build task, not a template
// to fill in — write the class yourself, per the framework's rule that
// Claude never writes the implementation.
//
// Reminder: this header will be included by multiple translation units once
// linked into a real build. Any out-of-class member function definitions
// you write here must be `inline` — same ODR lesson from thread_guard (2.1),
// account_transfer (3.2.4), and every build task since.

template <typename T>
class threadsafe_queue {
private:
    mutable std::mutex mut;
    std::condition_variable data_cond;
    std::queue<T> data_queue;

public:
    threadsafe_queue();
    threadsafe_queue(const threadsafe_queue& other);
    threadsafe_queue& operator=(const threadsafe_queue& other) = delete;
    void push(T new_value);
    bool try_pop(T& value);
    std::shared_ptr<T> try_pop();
    void wait_and_pop(T& value);
    std::shared_ptr<T> wait_and_pop();
    bool empty() const;
    int size() const;
};

template <typename T>
inline threadsafe_queue<T>::threadsafe_queue()
    : mut()
    , data_cond()
    , data_queue()
{
}

template <typename T>
inline threadsafe_queue<T>::threadsafe_queue(const threadsafe_queue& other)
    : mut()
    , data_cond()
    , data_queue()
{
    std::lock_guard<std::mutex> lk(other.mut);
    // I think this assumes that the copy assignment operator
    // in std::queue is implemented to give a deep copy.
    data_queue = other.data_queue;
}

template <typename T>
inline void threadsafe_queue<T>::push(T new_value)
{
    std::lock_guard<std::mutex> lk(mut);
    data_queue.push(new_value);
    data_cond.notify_one();
}

template <typename T>
inline bool threadsafe_queue<T>::try_pop(T& value)
{
    std::lock_guard<std::mutex> lk(mut);
    if (data_queue.empty()) {
        return false;
    }
    value = data_queue.front();
    data_queue.pop();
    return true;
}

template <typename T>
inline std::shared_ptr<T> threadsafe_queue<T>::try_pop()
{
    std::lock_guard<std::mutex> lk(mut);
    if (data_queue.empty()) {
        // return std::shared_ptr<T>();
        return nullptr;
    }
    std::shared_ptr<T> res(std::make_shared<T>(data_queue.front()));
    data_queue.pop();
    return res;
}

template <typename T>
inline void threadsafe_queue<T>::wait_and_pop(T& value)
{
    std::unique_lock<std::mutex> u_lk(mut);
    data_cond.wait(u_lk, [this]() { return !this->data_queue.empty(); });
    value = data_queue.front();
    data_queue.pop();
}

template <typename T>
inline std::shared_ptr<T> threadsafe_queue<T>::wait_and_pop()
{
    std::unique_lock<std::mutex> u_lk(mut);
    data_cond.wait(u_lk, [this]() { return !this->data_queue.empty(); });
    std::shared_ptr<T> res(std::make_shared<T>(data_queue.front()));
    data_queue.pop();
    return res;
}

template <typename T>
inline bool threadsafe_queue<T>::empty() const
{
    std::lock_guard<std::mutex> lk(mut);
    return data_queue.empty();
}

template <typename T>
inline int threadsafe_queue<T>::size() const
{
    std::lock_guard<std::mutex> lk(mut);
    return data_queue.size();
}
