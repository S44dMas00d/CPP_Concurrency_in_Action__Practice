#pragma once
#include <algorithm>
#include <atomic>
#include <exception>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <stack>
#include <thread>
#include <vector>

// first I need to make a thread safe simple stack just for making this
// assignment, so we are going to stick to a stack that is based on the
// more coarse mutex. so here goes:

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
    std::stack<std::shared_ptr<T>> data;

public:
    threadsafe_stack()
        : m()
        , data()
    {
    }

    void push(const T& new_value)
    {
        std::shared_ptr<T> new_val_ptr = std::make_shared<T>(new_value);
        std::lock_guard<std::mutex> lk(m);
        data.push(std::move(new_val_ptr));
    }

    void push(T&& new_value)
    {
        std::shared_ptr<T> new_val_ptr = std::make_shared<T>(std::move(new_value));
        std::lock_guard<std::mutex> lk(m);
        data.push(std::move(new_val_ptr));
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lk(m);
        return data.empty();
    }

    std::shared_ptr<T> pop()
    {
        std::lock_guard<std::mutex> lk(m);
        if (data.empty()) {
            return std::shared_ptr<T>();
        }

        std::shared_ptr<T> res = data.top();
        data.pop();
        return res;
    }
};

// hardware_threads - 1, leaving room for the thread that calls do_sort.
// When hardware_threads is 0 (hardware_concurrency() couldn't tell): 1,
// matching parallel_accumulate's fallback of 2 threads in total.
unsigned default_max_workers(unsigned hardware_threads)
{
    // unsigned hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads == 0) {
        return 1;
    }
    return hardware_threads - 1;
}

template <typename T>
class sorter {
private:
    struct chunk_to_sort {
        std::list<T> data;
        std::promise<std::list<T>> promise;
    };

    int max_thread_count;
    std::mutex m;
    std::vector<std::thread> workers;
    std::atomic<bool> end_of_data;
    std::atomic<bool> do_sort_running;
    std::atomic<int> num_workers_running;
    threadsafe_stack<chunk_to_sort> chunks;

public:
    // cap = default_max_workers(std::thread::hardware_concurrency())
    sorter()
        : max_thread_count(default_max_workers(std::thread::hardware_concurrency()))
        , workers(max_thread_count)
    {

        end_of_data.store(false);
        do_sort_running.store(false);
        num_workers_running.store(0);
    };
    // cap given directly (for tests); 0 is allowed
    explicit sorter(unsigned max_workers)
        : max_thread_count(default_max_workers(max_workers))
        , workers(max_thread_count)
    {
        end_of_data.store(false);
        do_sort_running.store(false);
        num_workers_running.store(0);
    };
    // stops every worker thread, then joins it
    ~sorter()
    {
        end_of_data.store(true);
        for (size_t i = 0; i < workers.size() && i < num_workers_running.load(); i++) {
            workers[i].join();
        }
        do_sort_running.store(false);
    };
    sorter(const sorter&) = delete;
    sorter& operator=(const sorter&) = delete;

    void try_sort_chunk()
    {
        std::shared_ptr<chunk_to_sort> chunk = chunks.pop();
        if (chunk) {
            do_sort_running.store(true);
            // run the function:
            chunk->promise.set_value(do_sort(chunk->data));
        }
    }

    void sort_thread()
    {
        while (!end_of_data) {
            try_sort_chunk();
            std::this_thread::yield();
        }
        do_sort_running.store(false);
    }

    // returns the sorted elements; chunk_data is consumed
    std::list<T> do_sort(std::list<T>& chunk_data)
    {
        if (chunk_data.empty()) {
            return chunk_data;
        }
        std::list<T> result;
        result.splice(result.begin(), chunk_data, chunk_data.begin());
        const T& partition_val = *result.begin();
        typename std::list<T>::iterator divide_point = std::partition(chunk_data.begin(),
            chunk_data.end(), [&](const T& val) { return val < partition_val; });
        chunk_to_sort new_lower_chunk;
        new_lower_chunk.data.splice(new_lower_chunk.data.end(),
            chunk_data, chunk_data.begin(), divide_point);
        std::future<std::list<T>> new_lower = new_lower_chunk.promise.get_future();
        chunks.push(std::move(new_lower_chunk));
        {
            std::lock_guard<std::mutex> lk(m);
            if (num_workers_running.load() < max_thread_count) {
                workers[num_workers_running.load()] = std::thread(&sorter<T>::sort_thread, this);
                ++num_workers_running;
            }
        }
        std::list<T> new_higher(do_sort(chunk_data));
        result.splice(result.end(), new_higher);
        while (new_lower.wait_for(std::chrono::seconds(0))
            != std::future_status::ready) {
            try_sort_chunk();
        }
        result.splice(result.begin(), new_lower.get());
        return result;
    };
    // worker threads started so far; only call it while no do_sort is running
    std::size_t worker_count() const
    {
        return static_cast<std::size_t>(num_workers_running.load());
    };
};

// one sorter per call, as in the book
template <typename T>
std::list<T> parallel_quick_sort(std::list<T> input)
{
    if (input.empty()) {
        return input;
    }
    sorter<T> s;
    return s.do_sort(input);
};
