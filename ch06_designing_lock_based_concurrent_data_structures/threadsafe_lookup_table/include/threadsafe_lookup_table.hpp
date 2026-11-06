#pragma once
#include "threadsafe_list.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class threadsafe_lookup_table {
private:
    using bucket_value = std::pair<Key, Value>;
    using bucket_data = threadsafe_list<bucket_value>;
    std::vector<std::unique_ptr<bucket_data>> buckets;
    Hash hasher;

public:
    using key_type = Key;
    using value_type = Value;
    using hash_type = Hash;

    explicit threadsafe_lookup_table(int num_buckets = 19, const Hash& hasher = Hash())
    {
        this->hasher = hasher;
        buckets.reserve(num_buckets);
        for (int i = 0; i < num_buckets; i++) {
            buckets.emplace_back(std::make_unique<bucket_data>());
        }
    };
    threadsafe_lookup_table(const threadsafe_lookup_table& other) = delete;
    threadsafe_lookup_table& operator=(const threadsafe_lookup_table& other) = delete;

    bucket_data& get_bucket(const Key& key) const
    {
        size_t const bucket_index = hasher(key) % buckets.size();
        return *buckets[bucket_index];
    };

    Value value_for(const Key& key, const Value& default_value = Value()) const
    {
        bucket_data& bucket = get_bucket(key);
        // find_first_copy_if (not find_first_if): entries can be rewritten
        // in place by add_or_update_mapping, so the value must be copied
        // out while the matching node's mutex is still held. Copying
        // through a shared_ptr AFTER find_first_if returned would race
        // with a concurrent update — the node locks are all released by
        // then, and the reader could observe a half-overwritten value
        // (the TEST 2 failure mode in the driver).
        std::shared_ptr<bucket_value> bucket_entry_found
            = bucket.find_first_copy_if(
                [&](const bucket_value& bucket_entry) {
                    return bucket_entry.first == key;
                });
        if (!bucket_entry_found) {
            return default_value;
        }
        // safe to read without a lock: this is a private copy of the
        // entry made under the node's lock, not the live entry itself
        return bucket_entry_found->second;
    };

    void add_or_update_mapping(const Key& key, const Value& value)
    {
        bucket_data& bucket = get_bucket(key);
        bucket.push_front_if_absent(
            [&](const bucket_value& bucket_entry) {
                return bucket_entry.first == key;
            },
            std::pair<Key, Value>(key, value));
        return;
    };

    void remove_mapping(const Key& key)
    {
        bucket_data& bucket = get_bucket(key);
        bucket.remove_if(
            [&](const bucket_value& bucket_entry) {
                return bucket_entry.first == key;
            });
    };

    std::map<Key, Value> get_map() const
    {
        std::map<Key, Value> res;
        for (size_t i = 0; i < buckets.size(); i++) {
            buckets[i]->for_each(
                [&](const bucket_value& bucket_entry) {
                    res.insert({ bucket_entry.first, bucket_entry.second });
                });
        }
        return res;
    };
};
