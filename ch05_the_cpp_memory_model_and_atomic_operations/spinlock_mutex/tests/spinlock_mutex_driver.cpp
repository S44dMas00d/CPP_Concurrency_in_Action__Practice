#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "spinlock_mutex.hpp"

// Structure to hold the exact interval a thread spent in the critical section
struct CriticalSectionInterval {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
};

int counter = 0;

int main()
{
    int num_threads_1 = 10;
    int sleep_duration_ms = 300;

    std::vector<std::thread> threads(num_threads_1);
    std::vector<CriticalSectionInterval> critical_intervals(num_threads_1);

    spinlock_mutex spin_mt;

    // Total wall-clock timing start ---
    auto total_wall_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_threads_1; i++) {
        threads[i] = std::thread([&, i]() {
            std::lock_guard<spinlock_mutex> lk(spin_mt);

            // Capture exact critical section interval
            // Timestamp immediately after acquiring the lock
            auto cs_start = std::chrono::steady_clock::now();

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration_ms));
            counter += 1;

            // Timestamp right before the lock_guard goes out of scope and releases
            auto cs_end = std::chrono::steady_clock::now();
            critical_intervals[i] = { cs_start, cs_end };
        });
    }

    for (int i = 0; i < num_threads_1; i++) {
        threads[i].join();
    }

    // ---Total wall-clock timing end & assert ---
    auto total_wall_end = std::chrono::steady_clock::now();
    auto total_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        total_wall_end - total_wall_start)
                                .count();

    // If the spinlock genuinely serializes, total time must be close to num_threads * sleep_time.
    // A broken lock allowing overlaps would finish in ~300ms.
    // We allow a small margin of error for thread scheduling overhead.
    long long expected_min_ms = (num_threads_1 * sleep_duration_ms) - 50;
    assert(total_elapsed_ms >= expected_min_ms && "Wall-clock time too short; lock did not serialize threads");
    std::cout << "[PASS] Total wall-clock serialization: " << total_elapsed_ms
              << "ms (Expected >= " << expected_min_ms << "ms)\n";

    // --- Assert exact expected final count ---
    assert(counter == num_threads_1 && "Counter does not match expected thread count");
    std::cout << "[PASS] Final counter value: " << counter << " == " << num_threads_1 << "\n";

    // --- Sort intervals and assert no overlap ---
    std::sort(critical_intervals.begin(), critical_intervals.end(),
        [](const CriticalSectionInterval& a, const CriticalSectionInterval& b) {
            return a.start < b.start;
        });

    for (size_t i = 1; i < critical_intervals.size(); ++i) {
        // Assert that each interval's start is >= the previous interval's end
        assert(critical_intervals[i].start >= critical_intervals[i - 1].end && "Critical sections overlapped in time!");
    }
    std::cout << "[PASS] No-overlap on critical-section intervals verified.\n";

    // --- EDGE CASE #2 DOCUMENTATION ---
    // Note: spinlock_mutex is NOT reentrant. Do not add a test that attempts to
    // lock this mutex twice from the same thread; it will hang (deadlock) indefinitely.

    return 0;
}