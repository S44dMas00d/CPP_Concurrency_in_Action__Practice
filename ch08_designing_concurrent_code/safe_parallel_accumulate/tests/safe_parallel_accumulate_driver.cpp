#include "safe_parallel_accumulate.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

class poison_error : public std::exception {
public:
    const char* what() const noexcept override
    {
        return "-1 encountered !";
    }
};

class throwing_type {
private:
    int internal_int;

public:
    throwing_type()
        : internal_int(0)
    {
    }
    throwing_type(int val)
        : internal_int(val)
    {
    }

    throwing_type& operator+=(const throwing_type& other)
    {
        internal_int += other.internal_int;
        return *this;
    }

    throwing_type operator+(const throwing_type& other) const
    {
        if (other.internal_int == -1) {
            throw poison_error();
        }

        throwing_type result = *this;
        result += other;
        return result;
    }
};

int main()
{
    // TEST # 1:
    // Correct sums. Sum the ints 1..10,000 with init = 7. Assert the result equals
    // std::accumulate with the same init. Also sum a 10-element range (one thread
    // only) and an empty range (returns init). Catches: init added once per block,
    // a dropped or double-counted block, and the last block lost.

    std::vector<int> test_values(10000);
    std::iota(test_values.begin(), test_values.end(), 1);
    int res_actual = safe_parallel_accumulate<
        std::vector<int>::iterator, int>(test_values.begin(), test_values.end(), 7);
    int res_compare = std::accumulate(test_values.begin(), test_values.end(), 7);
    assert(res_actual == res_compare);

    std::vector<int> test_values_1(10);
    std::iota(test_values_1.begin(), test_values_1.end(), 1);
    int res_actual_1 = safe_parallel_accumulate<
        std::vector<int>::iterator, int>(test_values_1.begin(), test_values_1.end(), 7);
    int res_compare_1 = std::accumulate(test_values_1.begin(), test_values_1.end(), 7);
    assert(res_actual_1 == res_compare_1);

    std::vector<int> test_values_2(0);
    int res_actual_2 = safe_parallel_accumulate<
        std::vector<int>::iterator, int>(test_values_2.begin(), test_values_2.end(), 7);
    int res_compare_2 = std::accumulate(test_values_2.begin(), test_values_2.end(), 7);
    assert(res_actual_2 == res_compare_2);

    // TEST # 2:
    // A worker's exception reaches the caller. Use 10,000 elements with -1 at index 0
    // (always in the first spawned block). Assert the call throws poison_error and
    // that the program carries on. Then run a clean sum, and assert it's right.
    // Catches: an exception escaping a spawned thread (std::terminate), and a future
    // that is never read.
    std::vector<throwing_type> test_2_values(10000);
    int temp_val = -1;
    for (size_t i = 0; i < test_2_values.size(); ++i) {
        test_2_values[i] = throwing_type(temp_val);
        temp_val++;
    }
    bool poison_error_caught = false;
    try {
        safe_parallel_accumulate<std::vector<throwing_type>::iterator, throwing_type>(
            test_2_values.begin(), test_2_values.end(), throwing_type());
    } catch (const poison_error& e) {
        std::cerr << e.what() << '\n';
        poison_error_caught = true;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    assert(poison_error_caught);

    // TEST # 3:
    // The calling thread's exception doesn't strand the workers. Use 10,000
    // elements with -1 at the last index, which is in the calling thread's own
    // block, while the workers are still joinable. Assert poison_error reaches
    // the caller. Then put -1 at both index 0 and the last index, and assert
    // exactly one poison_error is caught. Catches: missing join_threads (a
    // joinable std::thread destroyed during unwinding calls std::terminate).
    // I checked in scratch that a version without it aborts on this test every time.
    std::vector<throwing_type> test_3_values_1(10000);
    for (size_t i = 0; i < test_3_values_1.size(); ++i) {
        test_3_values_1[i] = throwing_type(
            i == test_3_values_1.size() - 1 ? -1 : static_cast<int>(i));
    }

    bool poison_error_caught_2 = false;
    try {
        safe_parallel_accumulate<std::vector<throwing_type>::iterator, throwing_type>(
            test_3_values_1.begin(), test_3_values_1.end(), throwing_type());
    } catch (const poison_error& e) {
        std::cerr << e.what() << '\n';
        poison_error_caught_2 = true;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    assert(poison_error_caught_2);

    std::vector<throwing_type> test_3_values_2(10000);
    for (size_t i = 0; i < test_3_values_2.size(); ++i) {
        int val = (i == 0 || i == test_3_values_2.size() - 1)
            ? -1
            : static_cast<int>(i);
        test_3_values_2[i] = throwing_type(val);
    }

    int poison_error_count = 0;

    try {
        safe_parallel_accumulate<std::vector<throwing_type>::iterator, throwing_type>(
            test_3_values_2.begin(), test_3_values_2.end(), throwing_type());
    } catch (const poison_error& e) {
        ++poison_error_count;
    }

    assert(poison_error_count == 1);

    // TEST # 4:
    // join_threads on its own. Make a vector of 4 threads, each sleeping about
    // 50 ms and then setting its own std::atomic<bool>. Add one default-constructed
    // std::thread to the vector. Let a join_threads go out of scope. Assert all
    // 4 flags are set and no thread in the vector is joinable(). Catches: a missed
    // join, and calling join() on a thread that isn't joinable (that throws
    // std::system_error).

    std::vector<std::thread> threads(4);
    std::vector<std::atomic<bool>> bools(threads.size());
    for (size_t i = 0; i < bools.size(); ++i) {
        bools[i].store(false);
    }

    for (size_t i = 0; i < threads.size(); i++) {
        threads[i] = std::thread([&, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            bools[i].store(true);
        });
    }
    // Default-constructed, not joinable.
    threads.push_back(std::thread());

    // Destructor joins all joinable threads.
    {
        join_threads joiner(threads);
    }

    for (size_t i = 0; i < bools.size(); ++i) {
        assert(bools[i].load());
    }

    for (const std::thread& t : threads) {
        assert(!t.joinable());
    }

    return 0;
}