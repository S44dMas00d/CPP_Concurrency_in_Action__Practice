#pragma once
#include <thread>

// thread_guard — build task for section 2.1 (Basic thread management).
//
// Interface, contract, and edge cases: see
// ch02_managing_threads/basic-thread-management-notes.md.
//
// Deliberately empty. Write the class yourself, per the framework's rule
// that Claude never writes the implementation.

class thread_guard {
private:
    std::thread& t;

public:
    explicit thread_guard(std::thread& t);
    thread_guard(thread_guard const&) = delete;
    thread_guard& operator=(thread_guard const&) = delete;
    ~thread_guard();
};

inline thread_guard::thread_guard(std::thread& t_)
    : t(t_)
{
}

inline thread_guard::~thread_guard()
{
    if (t.joinable()) {
        t.join();
    }
}
