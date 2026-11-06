#pragma once
#include <exception>
#include <memory>
#include <mutex>
#include <stack>

// threadsafe_stack — build task for section 3.2.3 (Spotting race conditions
// inherent in interfaces).
//
// Interface, contract, and edge cases: see
// ch03_sharing_data_between_threads/protecting-shared-data-with-mutexes-notes.md.
//
// Deliberately empty beyond the includes you'll likely need. Write the class
// yourself, per the framework's rule that Claude never writes the
// implementation.
//
// Reminder: this repo builds as C++17. The book writes
// `const char* what() const throw();` for empty_stack — that's pre-C++17
// syntax (dynamic exception specifications were removed in C++17). Use
// `const char* what() const noexcept override;` instead.

struct empty_stack : std::exception {

    const char* what() const noexcept override
    {
        return "empty stack";
    }
};

template <typename T>
class threadsafe_stack {
private:
    mutable std::mutex m;
    std::stack<T> data;

public:
    threadsafe_stack();
    threadsafe_stack(const threadsafe_stack& other);
    threadsafe_stack& operator=(const threadsafe_stack&) = delete;

    void push(T new_value);
    std::shared_ptr<T> pop();
    void pop(T& value);
    bool empty() const;
};

template <typename T>
threadsafe_stack<T>::threadsafe_stack()
    : m()
    , data()
{
}

template <typename T>
threadsafe_stack<T>::threadsafe_stack(const threadsafe_stack& other)
{
    std::lock_guard<std::mutex> lk(other.m);
    data = other.data;
}

template <typename T>
std::shared_ptr<T> threadsafe_stack<T>::pop()
{
    std::lock_guard<std::mutex> lk(m);
    if (data.empty()) {
        throw empty_stack();
    }
    std::shared_ptr<T> res = std::make_shared<T>(data.top());
    data.pop();
    return res;
}

template <typename T>
void threadsafe_stack<T>::pop(T& value)
{
    std::lock_guard<std::mutex> lk(m);
    if (data.empty()) {
        throw empty_stack();
    }
    value = data.top();
    data.pop();
}

template <typename T>
void threadsafe_stack<T>::push(T new_value)
{
    std::lock_guard<std::mutex> lk(m);
    data.push(std::move(new_value));
}

template <typename T>
bool threadsafe_stack<T>::empty() const
{
    std::lock_guard<std::mutex> lk(m);
    return data.empty();
}