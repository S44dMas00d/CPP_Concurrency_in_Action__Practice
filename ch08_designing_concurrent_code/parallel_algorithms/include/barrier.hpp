#pragma once
#include <atomic>
#include <thread>

class barrier {
private:
    std::atomic<int> count;
    std::atomic<int> spaces;
    std::atomic<int> generation;

public:
    explicit barrier(int count_)
    {
        count.store(count_);
        spaces.store(count_);
        generation.store(0);
    }

    void wait()
    {
        int const my_generation = generation;
        if (!--spaces) {
            spaces.store(count);
            ++generation;
        } else {
            while (generation == my_generation) {
                std::this_thread::yield();
            }
        }
    }

    void done_waiting()
    {
        --count;
        if (!--spaces) {
            spaces = count.load();
            ++generation;
        }
    }
};