#pragma once
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
// shared_mutex_cache — build task for section 3.3.2 (Protecting rarely
// updated data structures).
//
// Interface, contract, and edge cases: see
// ch03_sharing_data_between_threads/alternative-facilities-for-protecting-shared-data-notes.md.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// class yourself, per the framework's rule that Claude never writes the
// implementation.
//
// Book uses boost::shared_mutex/boost::shared_lock (no reader-writer lock
// existed in the standard when the book was written). This repo builds as
// C++17, which standardized the book's own proposal as std::shared_mutex
// (<shared_mutex>) + std::shared_lock — same semantics, use those instead,
// no Boost dependency needed.
//
// Reminder: this header will be included by multiple translation units once
// linked into a real build. Any out-of-class member function definitions
// you write here must be `inline` — same ODR lesson from thread_guard (2.1)
// and account_transfer (3.2.4).

template <typename Key, typename Value>
class shared_mutex_cache {

private:
    std::map<Key, Value> entries;
    mutable std::shared_mutex sh_mt;

public:
    // these are extra fittings being added to test out the concurrency
    mutable std::mutex mt;
    mutable int active_no_threads = 0;
    mutable int peak_no_threads = 0;
    shared_mutex_cache() = default;

    // shared (read) access — mirrors
    // find_entry(): returns a
    // default-constructed Value{} if
    // the key isn't present, same as
    // the book's dns_entry() fallback
    Value find(Key const& key) const
    {
        std::shared_lock<std::shared_mutex> sh_lk(sh_mt);
        // here we need to serialize the shared for just a second
        // to check how many are now holding a shared lock and are
        // performing were going to get a shared read below after the
        // concurrent shared_mutex lock they have
        std::unique_lock<std::mutex> unq_lk(mt);
        ++active_no_threads;
        if (active_no_threads > peak_no_threads) {
            peak_no_threads = active_no_threads;
        }
        unq_lk.unlock();
        // artificial delay so the active-readers window is actually wide enough to
        // observe overlap. without this, the increment-to-decrement window is just
        // one map::find() (nanoseconds), so even with 30 real concurrent readers
        // under the shared_lock, each thread races through increment -> find ->
        // decrement and re-wins the `mt` lock before the OS schedules any other
        // waiting thread (unlocking a mutex on linux doesn't force a context
        // switch) -- so active_no_threads never gets observed above 1. widening
        // this window is what lets genuine concurrency actually show up in the
        // counter.

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        typename std::map<Key, Value>::const_iterator it = entries.find(key);
        // serialize here as well so that those threads can remove themselves
        // from the active number tracker for their run before leaving the func
        unq_lk.lock();
        --active_no_threads;
        unq_lk.unlock();
        return (it == entries.end()) ? Value() : it->second;
    }

    // exclusive (write) access
    void update_or_add(Key const& key, Value const& value)
    {
        std::unique_lock<std::shared_mutex> unq_lk(sh_mt);
        entries[key] = value;
    }
};
