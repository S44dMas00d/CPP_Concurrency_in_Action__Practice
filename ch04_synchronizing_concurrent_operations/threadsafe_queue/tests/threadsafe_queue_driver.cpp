#include "threadsafe_queue.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>
// Concurrent driver for the threadsafe_queue build task (section 4.1.2,
// "Building a thread-safe queue with condition variables").
//
// Requirements from the build spec — see
// ch04_synchronizing_concurrent_operations/waiting-for-an-event-or-condition-notes.md
// for full detail:
//  - Real conservation stress test: multiple producers, multiple consumers,
//    real volume (hundreds+ items) -- nothing lost, nothing duplicated.
//  - Two consumers both blocked in wait_and_pop() when exactly one item
//    arrives -- no hang, no duplicate delivery.
//  - try_pop() must never block, even under concurrent calls against an
//    empty queue -- prove it, don't just assume it from "the test was fast."
//  - wait_and_pop() must genuinely block and wake on notification (not
//    busy-poll) -- prove it with timestamps around a deliberately delayed
//    push(), not just "it eventually returned."
//  - Must be ThreadSanitizer-clean (enabled by default via the root
//    CMakeLists' ENABLE_TSAN option -- no extra flags needed).

int main()
{
    // TEST_1:
    // Real conservation stress test, same shape as threadsafe_stack's:
    // multi-producer / multi-consumer driver, closed-book.
    // Real conservation stress test, same shape as
    // threadsafe_stack's: multiple producer threads pushing,
    // multiple consumer threads draining concurrently, real
    // volume (hundreds+ items, not a handful). Confirm
    // everything pushed is either popped exactly once or still
    // in the queue at the end — nothing lost, nothing
    // duplicated, nothing double-read.
    threadsafe_queue<int> TS_Queue = threadsafe_queue<int>();
    assert(TS_Queue.empty() && "Before all the pushes - TS_Queue should be empty");
    // ok so for the first thing we need to check if the multiple producers can together
    // work and end up with the conservative number of values after
    // the threads are joined, i have added the size() function as
    // an extra function similar to how empty() works with the mutex.
    std::thread t1([&TS_Queue]() {
        for (int i = 0; i < 50; i++) {
            TS_Queue.push(i);
        }
    });
    std::thread t2([&TS_Queue]() {
        for (int i = 50; i < 100; i++) {
            TS_Queue.push(i);
        }
    });
    t1.join();
    t2.join();
    assert(!TS_Queue.empty() && "After all the pushes - TS_Queue should not be empty");
    assert(TS_Queue.size() == 100 && "After 100 concurrent pushes - TS_Queue should be 100 elements");

    // Now we need to drain the queue and test concurrent pops but since we
    // already have a full queue and dont necessarily get notify() anymore,
    // this is the best time to test the try_pop() functions.
    std::vector<int> t3_pop_res;
    std::vector<int> t4_pop_res;
    // bool try_pop(T & value);
    // std::shared_ptr<T> try_pop();
    std::thread t3([&TS_Queue, &t3_pop_res]() {
        while (true) {
            int temp;
            if (!TS_Queue.try_pop(temp)) {
                return;
            }
            t3_pop_res.push_back(temp);
        }
    });
    std::thread t4([&TS_Queue, &t4_pop_res]() {
        while (true) {
            std::shared_ptr<int> temp_ptr = TS_Queue.try_pop();
            if (!temp_ptr) {
                return;
            }
            t4_pop_res.push_back(*temp_ptr);
        }
    });
    t3.join();
    t4.join();
    assert(TS_Queue.empty() && "After all the try_pops - TS_Queue should be empty");
    // we now combine the two vectors and check to ensure both
    // are combined giving the same no of 100 elements that
    // we inserted and now popped off
    t3_pop_res.insert(t3_pop_res.end(), t4_pop_res.begin(), t4_pop_res.end());
    assert(t3_pop_res.size() == 100 && "After all the try_pops the combined resulting array should have the same 100 size.");

    // TEST_2:
    // Two consumers both blocked in wait_and_pop() when exactly one item
    // arrives. notify_one() only guarantees waking *at most* one waiter,
    // with no guarantee which one -- so with two consumers parked on an
    // empty queue and a single push, exactly one of them should receive
    // that item; the other must keep genuinely waiting, not spin, not
    // throw, not silently end up with a duplicate of the value it never
    // got. We push a second, distinct item afterward so the other
    // consumer can also complete -- that's what lets us join both
    // threads without the test itself hanging forever.
    assert(TS_Queue.empty() && "TS_Queue should be empty going into the two-consumer test");

    int t2_consumer_1_res = -1;
    int t2_consumer_2_res = -1;
    std::thread t2_consumer_1([&]() { TS_Queue.wait_and_pop(t2_consumer_1_res); });
    std::thread t2_consumer_2([&]() { TS_Queue.wait_and_pop(t2_consumer_2_res); });

    // give both consumers a real chance to actually park inside
    // data_cond.wait() before anything gets pushed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TS_Queue.push(111);
    // let whichever consumer woke land its pop before we hand the
    // second (still-blocked) consumer something to wake up for
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    TS_Queue.push(222);

    t2_consumer_1.join();
    t2_consumer_2.join();

    bool exactly_one_item_each = (t2_consumer_1_res == 111 && t2_consumer_2_res == 222)
        || (t2_consumer_1_res == 222 && t2_consumer_2_res == 111);
    assert(exactly_one_item_each && "each of the two consumers ought to receive exactly one of the two distinct pushed items -- no duplication, nothing lost");
    assert(TS_Queue.empty() && "TS_Queue should be empty again after both consumers popped their item");

    // TEST_3:
    // Prove try_pop() genuinely never blocks, not just
    // "the test happened to run fast." Call it repeatedly
    // (or concurrently, from several threads) against a queue
    // that stays empty the whole time, and confirm every call
    // returns promptly (false / null) rather than any thread
    // ever stalling.
    int num_threads_1 = 10;
    std::vector<std::thread> threads(num_threads_1);
    std::vector<std::chrono::steady_clock::duration> elapsed_times(num_threads_1);

    for (int i = 0; i < num_threads_1; i++) {
        threads[i] = std::thread([&, i]() {
            int res;
            auto start = std::chrono::steady_clock::now();
            TS_Queue.try_pop(res);
            // auto elapsed = std::chrono::steady_clock::now() - start;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            elapsed_times[i] = elapsed;
        });
    }

    for (int i = 0; i < num_threads_1; i++) {
        threads[i].join();
    }

    bool all_below_1_ms = std::all_of(
        elapsed_times.begin(),
        elapsed_times.end(),
        [](std::chrono::steady_clock::duration elapsed) { return elapsed < std::chrono::milliseconds(1); });

    assert(all_below_1_ms && "All try_pop attempts on the empty queue ought to be below 1ms");

    // TEST_4:
    // first we make a thread thats a consumer thats gonna wait_and_pop()
    // on the currently empty thread:
    int t5_res;
    auto after_pop = std::chrono::steady_clock::now();
    std::thread t5([&]() {
        TS_Queue.wait_and_pop(t5_res);
        after_pop = std::chrono::steady_clock::now();
    });
    // lets now make it wait
    auto before_push = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    TS_Queue.push(20);
    auto after_push = std::chrono::steady_clock::now();
    t5.join();
    auto elapsed_bw_push_pop = std::chrono::duration_cast<std::chrono::milliseconds>(after_pop - before_push);

    assert(elapsed_bw_push_pop >= std::chrono::milliseconds(300)
        && elapsed_bw_push_pop < std::chrono::milliseconds(350)
        && "wait_and_pop() ought to wake promptly after push -- not before the deliberate "
           "300ms delay (proves it isn't returning early / busy-returning on an empty queue), "
           "and not lagging well past it either (proves it isn't stuck / missing the notify).");

    // TEST_5:
    // empty() is a const member function, and the copy constructor takes
    // its `other` by const threadsafe_queue&, yet both need to lock
    // `mut` on that const object -- locking a std::mutex calls its
    // non-const lock()/unlock(), which you cannot normally invoke on a
    // const object. This compiles only because `mut` is declared
    // `mutable std::mutex mut;` -- `mutable` strips const-ness for that
    // one member specifically, so mutating it (i.e. locking/unlocking)
    // through a const object or a const& is still legal. The mutex
    // isn't part of the queue's logical/observable state -- toggling it
    // doesn't change what the queue *means* from the outside -- so this
    // doesn't actually violate const-correctness, only its literal
    // bitwise reading. Same reasoning already used for account_transfer's
    // std::lock()-guarded transfers and shared_mutex_cache's
    // `mutable std::shared_mutex sh_mt`.
    //
    // Prove both const access paths actually compile and work, not just
    // that the reasoning sounds right:
    threadsafe_queue<int> const& const_ref_to_queue = TS_Queue;
    assert(const_ref_to_queue.empty() && "calling empty() through a const& must compile and return correctly -- only possible because mut is mutable");

    TS_Queue.push(42);
    threadsafe_queue<int> const& another_const_ref = TS_Queue;
    // copy ctor's parameter is `const threadsafe_queue&` -- it locks
    // other.mut internally, which again only compiles because mut is mutable
    threadsafe_queue<int> copied_queue(another_const_ref);
    assert(copied_queue.size() == 1 && "copy constructor must deep-copy through a const& source");

    int copied_val = -1;
    assert(copied_queue.try_pop(copied_val) && copied_val == 42 && "copied queue should contain the same data as the source at the time of copying");

    int original_val = -1;
    assert(TS_Queue.try_pop(original_val) && original_val == 42 && "original queue must be unaffected by the copy -- still holds its own 42");
    assert(TS_Queue.empty() && copied_queue.empty() && "both queues should be empty after draining their own copy of the item");

    std::cout << "All Tests Passed" << std::endl;
    return 0;
}
