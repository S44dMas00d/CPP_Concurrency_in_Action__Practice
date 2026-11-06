#include "threadsafe_lookup_table.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

// key type for TEST # 4: operator== sleeps 200 ms when both sides have id 2
// and the flag is set, simulating a slow comparison on one specific node
struct slow_eq_key {
    int id;
    inline static bool sleep_on_two = false;

    bool operator==(const slow_eq_key& other) const
    {
        if (sleep_on_two && id == 2 && other.id == 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return id == other.id;
    }
};

struct slow_eq_key_hash {
    size_t operator()(const slow_eq_key& key) const
    {
        return std::hash<int> {}(key.id);
    }
};

int main()
{
    // TEST # 1:
    // Many threads adding the same new keys never create a duplicate. Use 1
    // bucket. 8 threads each call add_or_update_mapping(k, thread_index) for
    // k = 0..499, all in the same order, released together. After joining,
    // for each k: assert value_for(k, -1) is one of the thread indexes, then
    // call remove_mapping(k) once and assert value_for(k, -1) == -1.
    // Catches: "find, then insert" done as two separate steps. Two threads
    // both miss the key and both insert it, and the duplicate survives one
    // removal.
    threadsafe_lookup_table<int, int> table(1);
    std::atomic<bool> test_1_start_flag(false);

    std::vector<std::thread> add_threads(8);
    for (size_t i = 0; i < add_threads.size(); ++i) {
        add_threads[i] = std::thread([&, i]() {
            while (!test_1_start_flag.load()) { };
            for (int k = 0; k < 500; ++k) {
                table.add_or_update_mapping(k, i);
            }
        });
    }

    // now we have all the tasks setup and blocking eight threads
    // we now need to start the process using the flag:
    test_1_start_flag.store(true);
    // we need to then join all the adders
    for (size_t i = 0; i < add_threads.size(); ++i) {
        add_threads[i].join();
    }

    // TEST 1 ASSERT: every key maps to one of the thread indexes, and a
    // single remove_mapping clears it completely. If two threads both missed
    // a key and both inserted it, the first value_for still returns a valid
    // thread index, but one removal only unlinks one of the duplicates, so
    // the second value_for returns the surviving copy instead of -1.
    const int num_keys = 500;
    const int num_threads = add_threads.size();
    for (int k = 0; k < num_keys; ++k) {
        int value = table.value_for(k, -1);
        assert(value >= 0 && value < num_threads
            && "key is missing or maps to a value no thread wrote");
        table.remove_mapping(k);
        assert(table.value_for(k, -1) == -1
            && "key survived one removal: it was inserted more than once");
    }
    std::cout << "TEST 1 PASSED" << std::endl;

    // TEST # 2:
    // Readers never see a half-written value. Use 3 buckets and keys 0..9,
    // all present at the start, with std::string values. 2 writer threads
    // each loop 20,000 times, writing a 64-character string of a single
    // letter (the letter changes each iteration) to key i % 10. 4 reader
    // threads each loop 20,000 times on value_for(i % 10) and assert all 64
    // characters are the same letter. TSAN is the main detector. Catches:
    // reading an entry through a pointer after its node is unlocked, while
    // a writer changes it.
    threadsafe_lookup_table<int, std::string> table_2(3);
    for (int k = 0; k < 10; ++k) {
        table_2.add_or_update_mapping(k, std::string(64, 'z'));
    }
    std::atomic<bool> test_2_start_flag(false);

    std::vector<std::thread> writer_threads(2);
    for (size_t w = 0; w < writer_threads.size(); ++w) {
        writer_threads[w] = std::thread([&, w]() {
            while (!test_2_start_flag.load()) { };
            for (int i = 0; i < 20000; ++i) {
                // each writer cycles through the alphabet at a different
                // phase, so a torn read shows two different letters
                char letter = static_cast<char>('a' + (i + w * 13) % 26);
                table_2.add_or_update_mapping(i % 10, std::string(64, letter));
            }
        });
    }

    std::vector<std::thread> reader_threads(4);
    for (size_t r = 0; r < reader_threads.size(); ++r) {
        reader_threads[r] = std::thread([&, r]() {
            while (!test_2_start_flag.load()) { };
            for (int i = 0; i < 20000; ++i) {
                std::string value = table_2.value_for(i % 10, std::string());
                assert(value.size() == 64
                    && "reader got a truncated or missing value");
                assert(std::all_of(value.begin(), value.end(),
                           [&](char c) { return c == value[0]; })
                    && "reader saw a half-written value: the string mixes two writers' letters");
            }
        });
    }

    // now we have all the tasks setup and blocking six threads
    // we now need to start the process using the flag:
    test_2_start_flag.store(true);
    for (size_t i = 0; i < writer_threads.size(); ++i) {
        writer_threads[i].join();
    }
    for (size_t i = 0; i < reader_threads.size(); ++i) {
        reader_threads[i].join();
    }
    std::cout << "TEST 2 PASSED" << std::endl;

    // TEST # 4:
    // A writer isn't held up by a slow node further down the bucket. Use 1
    // bucket and a key type whose operator== sleeps 200 ms when both sides
    // have id 2 and a global flag is set. Insert keys 1 and 2 so key 1's
    // entry comes before key 2's in the list (the table push_fronts new
    // entries, so key 2 goes in first). Set the flag. Thread A calls
    // value_for(key 2); the matching comparison sleeps while key 2's node
    // is locked. The main thread waits about 50 ms, then times
    // add_or_update_mapping(key 1, new value). Assert it returns in well
    // under 50 ms. Catches: a bucket-wide lock around the normal operations
    // (with the book's shared_mutex, the writer waits for the reader), or
    // an update that walks past its own key's entry.
    slow_eq_key::sleep_on_two = false;
    threadsafe_lookup_table<slow_eq_key, int, slow_eq_key_hash> table_4(1, slow_eq_key_hash {});
    table_4.add_or_update_mapping(slow_eq_key { 2 }, 20);
    table_4.add_or_update_mapping(slow_eq_key { 1 }, 10);

    slow_eq_key::sleep_on_two = true;
    std::thread thread_a([&]() {
        int value = table_4.value_for(slow_eq_key { 2 }, -1);
        assert(value == 20 && "value_for(key 2) returned the wrong value");
    });

    // give thread A time to walk past key 1's node and start the 200 ms
    // comparison sleep while holding key 2's node lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto update_start = std::chrono::steady_clock::now();
    table_4.add_or_update_mapping(slow_eq_key { 1 }, 11);
    const auto update_elapsed = std::chrono::steady_clock::now() - update_start;

    thread_a.join();

    // the update only touches the head and key 1's node, both of which
    // thread A has already passed and released; a bucket-wide lock (the
    // book's shared_mutex design) would block here for the ~150 ms left in
    // the comparison sleep, and an update that kept walking past key 1
    // would stall on key 2's locked node
    assert(update_elapsed < std::chrono::milliseconds(50)
        && "add_or_update_mapping blocked behind a slow node further down the bucket");
    assert(table_4.value_for(slow_eq_key { 1 }, -1) == 11
        && "the update to key 1 was lost");
    std::cout << "TEST 4 PASSED" << std::endl;

    // TEST # 3:
    // get_map() is a snapshot of one moment. Use the default 19 buckets.
    // One writer thread adds keys 0..1,999 in increasing order (value =
    // key). 3 threads call get_map() in a loop until the writer finishes.
    // For every snapshot, assert that its keys are exactly 0..size-1 with
    // no gaps, and that each value equals its key. Catches: a get_map()
    // that reads the buckets one at a time without holding them all, so it
    // can see a later key but miss an earlier one.
    threadsafe_lookup_table<int, int> table_3;
    std::atomic<bool> writer_done(false);

    std::thread writer_thread([&]() {
        for (int k = 0; k < 2000; ++k) {
            table_3.add_or_update_mapping(k, k);
        }
        writer_done.store(true);
    });

    std::vector<std::thread> snapshot_threads(3);
    for (size_t i = 0; i < snapshot_threads.size(); ++i) {
        snapshot_threads[i] = std::thread([&, i]() {
            while (!writer_done.load()) {
                std::map<int, int> snapshot = table_3.get_map();
                // the writer adds keys strictly in increasing order, so any
                // single-moment view must be the prefix 0..size-1; std::map
                // iterates in sorted order, making gaps easy to spot
                int expected = 0;
                for (const auto& entry : snapshot) {
                    assert(entry.first == expected
                        && "snapshot contains a later key but missed an earlier one");
                    assert(entry.second == entry.first
                        && "snapshot value does not equal its key");
                    ++expected;
                }
            }
        });
    }

    writer_thread.join();
    for (size_t i = 0; i < snapshot_threads.size(); ++i) {
        snapshot_threads[i].join();
    }

    // final snapshot after the writer is done: all 2,000 keys, complete
    std::map<int, int> final_snapshot = table_3.get_map();
    assert(final_snapshot.size() == 2000
        && "keys were lost or duplicated during the run");
    int expected_key = 0;
    for (const auto& entry : final_snapshot) {
        assert(entry.first == expected_key && entry.second == expected_key
            && "final snapshot is incomplete or corrupted");
        ++expected_key;
    }
    std::cout << "TEST 3 PASSED" << std::endl;

    return 0;
}
