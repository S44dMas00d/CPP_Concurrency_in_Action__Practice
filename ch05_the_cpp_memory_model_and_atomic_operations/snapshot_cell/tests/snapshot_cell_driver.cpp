#include "snapshot_cell.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

int main()
{
    config initial { 0, 0 };
    snapshot_cell<config> cell(std::make_shared<const config>(initial));
    // TEST # 1:
    // ok so we need to have 5 threads working concurrently
    // one will be calling store with (i,i) from 1 to 200000 and
    // the other four will be doing concurrent reads and we ought
    // to do a tests where i == i for each load.
    std::atomic<bool> readsDone(false);
    // std::promise<bool> writesOverPromise;
    // std::future<bool> writesOverFuture = writesOverPromise.get_future();
    std::vector<std::shared_ptr<const config>>
        persistent_config_refs(200000, nullptr);
    // this vector is needed because if not made the reference count
    // would go to 0 otherwise if store keeps getting called and loads
    // get called late, not to mention the loads themselves are only
    // for local asserts() to check i==i and nothing else.
    std::thread t1([&]() {
        for (int i = 1; i <= 200000; i++) {
            persistent_config_refs[i - 1]
                = std::make_shared<const config>(config { i, i });
            cell.store(persistent_config_refs[i - 1]);
        }
        // writesOverPromise.set_value(true);
        readsDone.store(true);
    });

    std::vector<std::thread> reader_threads(4);
    for (size_t i = 0; i < reader_threads.size(); i++) {
        reader_threads[i] = std::thread([&]() {
            while (true) {
                // do work here till writes are at an end.
                std::shared_ptr<const config> local_ref = cell.load();
                assert(local_ref->fee == local_ref->limit
                    && "the i == i condition must be upheld at all times for concurrent stores and loads");
                if (readsDone.load()) {
                    break;
                }
            }
        });
    };

    // join them threads
    for (size_t i = 0; i < reader_threads.size(); i++) {
        reader_threads[i].join();
    }
    t1.join();

    // TEST # 2
    config initial2 { 0, 0 };
    snapshot_cell<config> cell2(std::make_shared<const config>(initial2));

    std::vector<std::thread> updater_threads(4);
    for (size_t i = 0; i < updater_threads.size(); i++) {
        updater_threads[i] = std::thread([&]() {
            for (int i = 0; i < 50000; i++) {
                cell2.update([](const config& current) {
                    config next = current;
                    next.fee++;
                    return next;
                });
            }
        });
    };
    // join them threads
    for (size_t i = 0; i < updater_threads.size(); i++) {
        updater_threads[i].join();
    }
    assert(cell2.load()->fee == 200000 && "no concurrent updated ought to have been lost.");
    std::cout << "All Tests Pass" << std::endl;
    return 0;
}