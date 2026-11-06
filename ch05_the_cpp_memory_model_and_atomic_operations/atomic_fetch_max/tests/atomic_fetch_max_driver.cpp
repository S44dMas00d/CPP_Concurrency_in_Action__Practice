#include "atomic_fetch_max.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

int main()
{
    // TEST # 1:
    int num_threads_1 = 10;
    std::atomic<int> global_max = 0;
    std::vector<std::thread> threads(num_threads_1);

    for (int i = 0; i < num_threads_1; i++) {
        threads[i] = std::thread([&, i]() {
            int limit = 10 * i;
            for (int j = 0; j <= limit; j++) {
                int old = fetchMax(global_max, j);
            }
        });
    }

    for (int i = 0; i < num_threads_1; i++) {
        threads[i].join();
    }

    assert((global_max.load() == 10 * (num_threads_1 - 1))
        && "global max should match the maximum possible global that could be assigned among all threads");
    std::cout << "test 1 global max => " << global_max.load() << std::endl;

    // TEST # 2:
    int max_val = global_max.load();
    int min_val = 0;
    int iterations_left = 100;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(min_val, max_val);

    int x = dist(gen);
    while (iterations_left) {
        int new_val = dist(gen);
        fetchMax(global_max, new_val);
        iterations_left--;
    }
    assert((global_max.load() == 10 * (num_threads_1 - 1))
        && "global max should match the maximum possible global that could be assigned among all threads");

    // TEST # 3:
    // A/B Arbitration Check
    std::vector<std::thread> threads_2(2);
    std::vector<int> wins(2, 0);
    std::atomic<int> global_max_2 = 0;
    for (int i = 0; i < 2; i++) {
        threads_2[i] = std::thread([&, i]() {
            for (int j = 0; j <= 100; j++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                int curr_old = fetchMax(global_max_2, j, wins[i]);
            }
        });
    }
    for (int i = 0; i < 2; i++) {
        threads_2[i].join();
    }
    assert(std::accumulate(wins.begin(), wins.end(), 0) == 100
        && "wins should all add up to 99 since we are trying to update max from 1 to 100.");

    std::cout << "thread 0 wins: " << wins[0] << " thread 1 wins:" << wins[1] << std::endl;
    std::cout << "all tests passed" << std::endl;
    return 0;
}
