#include "async_find.hpp"
#include "async_for_each.hpp"
#include "pairwise_partial_sum.hpp"
#include "parallel_find.hpp"
#include "parallel_for_each.hpp"
#include "parallel_partial_sum.hpp"
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
        return "poison_error encountered !";
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

    bool operator==(const throwing_type& other) const
    {
        if (internal_int == 100) {
            throw poison_error();
        }
        return internal_int == other.internal_int;
    }
};

int main()
{
    // TEST # 1:
    // parallel_for_each and async_for_each (8.7, 8.8).
    // - Fill 10,000 ints with 0..9,999 and apply f = [](int& x){ ++x; }.
    //   Assert element i is i + 1.
    // - Then use an f that throws a poison_error on the element equal to 5.
    //   Assert the caller catches it.
    // Do both checks for each function. Catches: a block skipped, done twice,
    // or the last block lost; an exception escaping a worker (std::terminate).
    std::vector<int> test_values(10000);
    std::vector<int> test_values_1(10000);
    std::iota(test_values.begin(), test_values.end(), 0);
    std::iota(test_values_1.begin(), test_values_1.end(), 0);

    async_for_each(test_values.begin(), test_values.end(), [](int& x) { ++x; });
    assert([&]() {
        for (size_t i = 0; i < test_values.size(); ++i) {
            if (i == 0 && test_values[i] != 1) {
                return false;
            }
            return test_values[i] == (test_values[i - 1] + 1);
        }
        return true;
    }());

    parallel_for_each(test_values_1.begin(), test_values_1.end(), [](int& x) { ++x; });
    assert([&]() {
        for (size_t i = 0; i < test_values_1.size(); ++i) {
            if (i == 0 && test_values_1[i] != 1) {
                return false;
            }
            return test_values_1[i] == (test_values_1[i - 1] + 1);
        }
        return true;
    }());

    bool poison_error_caught = false;
    try {
        async_for_each(test_values.begin(), test_values.end(), [](int& x) { 
            if(x == 5){
                throw poison_error();
            } });
    } catch (const poison_error& e) {
        std::cerr << e.what() << '\n';
        poison_error_caught = true;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    assert(poison_error_caught);

    poison_error_caught = false;
    try {
        parallel_for_each(test_values_1.begin(), test_values_1.end(), [](int& x) { 
            if(x == 5){
                throw poison_error();
            } });
    } catch (const poison_error& e) {
        std::cerr << e.what() << '\n';
        poison_error_caught = true;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    assert(poison_error_caught);

    // TEST # 2:
    // parallel_find and async_find (8.9, 8.10). On 10,000 distinct ints 0..9,999:
    // - finding 7,000 returns an iterator at position 7,000;
    // - finding -5 returns last.
    // For parallel_find only, also search for an absent value in a range of a
    // driver type whose operator== throws poison_error on one element (put it at
    // index 100), and assert the caller catches it. Catches: a block never searched;
    // waiting for ever on the promise when the value is absent; a lost exception.
    std::vector<int> test_values_2(10000);
    std::iota(test_values_2.begin(), test_values_2.end(), 0);
    std::vector<int>::iterator res_1_test_2
        = async_find(test_values_2.begin(), test_values_2.end(), 7000);
    std::vector<int>::iterator res_2_test_2
        = async_find(test_values_2.begin(), test_values_2.end(), -5);
    std::vector<int>::iterator res_3_test_2
        = parallel_find(test_values_2.begin(), test_values_2.end(), 7000);
    std::vector<int>::iterator res_4_test_2
        = parallel_find(test_values_2.begin(), test_values_2.end(), -5);
    assert(*res_1_test_2 == 7000);
    assert(res_2_test_2 == test_values_2.end());
    assert(*res_3_test_2 == 7000);
    assert(res_4_test_2 == test_values_2.end());

    std::vector<throwing_type> test_2_values_2(10000);
    int temp_val = 0;
    for (size_t i = 0; i < test_2_values_2.size(); ++i) {
        test_2_values_2[i] = throwing_type(temp_val);
        temp_val++;
    }
    poison_error_caught = false;
    try {
        std::vector<throwing_type>::iterator res_5_test_2
            = parallel_find(test_2_values_2.begin(),
                test_2_values_2.end(), throwing_type(5000));
    } catch (const poison_error& e) {
        std::cerr << e.what() << '\n';
        poison_error_caught = true;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    assert(poison_error_caught);

    // TEST # 3:
    // parallel_partial_sum (8.11). For lengths 1, 10 (one thread), 10,000 and
    // 10,001, fill long longs with i % 7 + 1. Assert the result equals
    // std::partial_sum on a copy. Catches: a missing or doubled forward add,
    // and off-by-one errors at block edges.
    const std::vector<size_t> lengths = { 1, 10, 10000, 10001 };

    for (size_t length : lengths) {
        std::vector<long long> values(length);

        for (size_t i = 0; i < length; ++i) {
            values[i] = static_cast<long long>(i % 7 + 1);
        }

        std::vector<long long> expected = values;
        std::partial_sum(expected.begin(), expected.end(), expected.begin());

        parallel_partial_sum(values.begin(), values.end());

        assert(values == expected);
    }

    // TEST # 4:
    // barrier (8.12/8.13).
    // - Lockstep: 4 threads × 1,000 rounds. In round r, each thread writes r to
    //   its own slot, calls wait(), checks all 4 slots equal r (counting mismatches
    //   in an atomic), then calls wait() again. Assert 0 mismatches. In scratch: 0
    //   mismatches in 3 of 3 TSAN runs.
    // - Dropout: barrier(4). One thread calls wait() 10 times, then done_waiting().
    //   Three threads call wait() 100 times. Assert all four finish. A
    //   done_waiting() that doesn't free its space hangs here; in scratch,
    //   verify.sh's timeout caught that.
    // Catches: a thread running ahead of the others, and a dropout that strands
    // the rest.

    // DIDN'T UNDERSTAND WHAT TO DO.

    // TEST # 5:
    // pairwise_partial_sum (8.13). For lengths 1, 2, 3, 100 and 127, fill long
    // longs with i % 7 + 1, and assert the result equals std::partial_sum on a
    // copy. Stay at or under about 200 elements, since it's one thread per element.
    // Catches: the printed listing's bug (it fails from length 3), and stale reads
    // across the two arrays.

    const std::vector<size_t> lengths_2 = { 1, 2, 3, 100, 127 };

    for (size_t length : lengths_2) {
        std::vector<long long> values(length);

        for (size_t i = 0; i < length; ++i) {
            values[i] = static_cast<long long>(i % 7 + 1);
        }

        std::vector<long long> expected = values;
        std::partial_sum(expected.begin(), expected.end(), expected.begin());

        pairwise_partial_sum(values.begin(), values.end());

        assert(values == expected);
    }

    return 0;
}