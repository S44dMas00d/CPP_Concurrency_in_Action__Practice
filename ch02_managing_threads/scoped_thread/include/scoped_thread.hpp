#pragma once
#include <stdexcept>
#include <thread>

// scoped_thread — build task for section 2.3 (Transferring ownership of a
// thread).
//
// Interface, contract, and edge cases: see
// ch02_managing_threads/transferring-ownership-of-a-thread-notes.md.
//
// Deliberately empty. Write the class yourself, per the framework's rule
// that Claude never writes the implementation.

class scoped_thread {
    std::thread t;

public:
    explicit scoped_thread(std::thread t_)
        : t(std::move(t_))
    {
        if (!t.joinable()) {
            throw std::logic_error("provided thread not joinable");
        }
    }
    ~scoped_thread()
    {
        //  i know that t is private and thus cant be joined externally
        // and thus this check is superflous - however, i prefer having it
        // here nonetheless as it wont harm anything either.
        if (t.joinable()) {
            t.join();
        }
    }
    // copy constructor and assignment deleted:
    scoped_thread(const scoped_thread& t_) = delete;
    scoped_thread& operator=(const scoped_thread& t_) = delete;
    // move constructor and assignment defined for containers to support:
    scoped_thread(scoped_thread&& other)
        : t(std::move(other.t))
    {
    }
    scoped_thread& operator=(scoped_thread&& other)
    {
        // compare memory addresses to make sure its not the same object
        if (this != &other) {
            if (t.joinable()) {
                t.join();
            }
            t = std::move(other.t);
        }
        // dereference the pointer and return this instance's actual object
        return *this;
    }
};
