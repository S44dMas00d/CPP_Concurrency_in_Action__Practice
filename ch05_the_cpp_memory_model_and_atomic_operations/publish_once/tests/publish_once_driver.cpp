#include "publish_once.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

int main()
{
    struct Data {
        int arr[1000];
        int value;
    };

    // TEST # 1 - One publisher, many readers, all fields visible.

    constexpr int NUM_ROUNDS = 200;
    constexpr int NUM_TRY_GET_THREADS = 4;
    constexpr int NUM_WAIT_THREADS = 4;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(1, 1000);

    for (int round = 0; round < NUM_ROUNDS; ++round) {

        // Fresh object and fresh value for every round.
        publish_once<Data> publish_once_instance;
        std::atomic<bool> start_flag { false };
        int k = dist(gen);

        std::thread publish_thread([&]() {
            while (!start_flag.load(std::memory_order_acquire)) {
            }

            Data data1;

            for (int i = 0; i < 1000; ++i) {
                data1.arr[i] = k;
            }

            data1.value = k * 2;

            publish_once_instance.publish(data1);
        });

        std::vector<std::thread> try_get_threads;
        std::vector<std::thread> wait_threads;

        // Polling readers
        for (int i = 0; i < NUM_TRY_GET_THREADS; ++i) {
            try_get_threads.emplace_back([&]() {
                while (!start_flag.load(std::memory_order_acquire)) {
                }

                const Data* ptr_to_data = nullptr;

                // Keep polling until publication happens.
                while ((ptr_to_data = publish_once_instance.try_get()) == nullptr) {
                }

                assert(ptr_to_data->value == k * 2);

                for (int j = 0; j < 1000; ++j) {
                    assert(ptr_to_data->arr[j] == k);
                }
            });
        }

        // Waiting readers
        for (int i = 0; i < NUM_WAIT_THREADS; ++i) {
            wait_threads.emplace_back([&]() {
                while (!start_flag.load(std::memory_order_acquire)) {
                }

                const Data& data = publish_once_instance.wait();

                assert(data.value == k * 2);

                for (int j = 0; j < 1000; ++j) {
                    assert(data.arr[j] == k);
                }
            });
        }

        // Release all threads at approximately the same time.
        start_flag.store(true, std::memory_order_release);
        publish_thread.join();
        for (std::thread& t : try_get_threads) {
            t.join();
        }
        for (std::thread& t : wait_threads) {
            t.join();
        }
    }

    // TEST # 2 - Racing publishers.
    for (int round = 0; round < NUM_ROUNDS; ++round) {

        // 8 threads racing to publish
        std::vector<std::thread> racing_threads(8);

        // Fresh publish_once object for every round
        publish_once<Data> publish_once_instance;

        // Start flag to release all publishers together
        std::atomic<bool> start_flag { false };

        // Count how many publishers successfully publish
        std::atomic<int> publish_success_count { 0 };

        for (size_t i = 0; i < racing_threads.size(); ++i) {

            racing_threads[i] = std::thread([&, i]() {
                // Wait for all publishers to be ready
                while (!start_flag.load(std::memory_order_acquire)) {
                }

                // Each publisher gets a distinct value
                int k = static_cast<int>(i) + 1;

                Data data;

                for (int j = 0; j < 1000; ++j) {
                    data.arr[j] = k;
                }

                data.value = k * 2;

                bool publish_success = publish_once_instance.publish(data);

                if (publish_success) {
                    publish_success_count.fetch_add(1);
                }
            });
        }

        // Release all publishers
        start_flag.store(true, std::memory_order_release);

        // Wait for all publishers to finish
        for (std::thread& t : racing_threads) {
            t.join();
        }

        // Exactly one publisher must have succeeded
        assert(
            publish_success_count.load() == 1 && "only one thread ought to succeed in publish");

        // The published object must be the winner's object.
        const Data& winner = publish_once_instance.wait();

        int winner_k = winner.value / 2;

        // Winner must have been one of the 8 publishers.
        assert(winner_k >= 1 && winner_k <= 8);

        // Verify the entire published object belongs to that winner.
        for (int j = 0; j < 1000; ++j) {
            assert(
                winner.arr[j] == winner_k && "published data must belong to the winning publisher");
        }
    }

    // TEST # 3 - Empty, then stable.
    {
        // Fresh object
        publish_once<Data> publish_once_instance;

        // Before publication, try_get() must return nullptr.
        const Data* ptr_to_data = publish_once_instance.try_get();

        assert(
            ptr_to_data == nullptr && "should be nullptr before object is published");

        // Create first object.
        int k = 3;

        Data data1;

        for (int i = 0; i < 1000; ++i) {
            data1.arr[i] = k;
        }

        data1.value = k * 2;

        // First publish must succeed.
        bool publish_success = publish_once_instance.publish(data1);

        assert(
            publish_success && "first publish ought to have succeeded");

        // Second publish must fail.
        Data data2;

        publish_success = publish_once_instance.publish(data2);

        assert(
            !publish_success && "second publish ought to have failed");

        // try_get() must now return a non-null pointer.
        const Data* ptr1 = publish_once_instance.try_get();

        assert(
            ptr1 != nullptr && "try_get() should return published object");

        // Repeated try_get() calls must return the same address.
        const Data* ptr2 = publish_once_instance.try_get();

        assert(
            ptr1 == ptr2 && "try_get() should return the same published object");

        // wait() must refer to the same object.
        const Data& ref_to_data = publish_once_instance.wait();

        assert(
            &ref_to_data == ptr1 && "wait() and try_get() should refer to the same object");

        // Verify the published value was not overwritten by data2.
        assert(ref_to_data.value == k * 2);

        for (int i = 0; i < 1000; ++i) {
            assert(
                ref_to_data.arr[i] == k && "published value should remain unchanged");
        }
    }
    std::cout << "All tests passed.\n";
}