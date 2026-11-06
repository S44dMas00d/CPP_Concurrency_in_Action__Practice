#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

template <typename T>
class threadsafe_list {
private:
    struct node {
        std::mutex m;
        std::shared_ptr<T> data_ptr;
        std::unique_ptr<node> next;
        node()
            : data_ptr(nullptr)
            , next(nullptr)
        {
        }
        node(const T& value)
            : data_ptr(std::make_shared<T>(value))
            , next(nullptr)
        {
        }
    };

    node head;

public:
    threadsafe_list()
        : head() {};

    threadsafe_list(const threadsafe_list& other) = delete;
    threadsafe_list& operator=(const threadsafe_list& other) = delete;

    void push_front(const T& value)
    {
        std::unique_ptr<node> new_node(std::make_unique<node>(value));
        std::lock_guard<std::mutex> lk(head.m);
        new_node->next = std::move(head.next);
        head.next = std::move(new_node);
    };

    // NECESSARY FOR WORKING WITH MAP WITHOUT THE RACE:
    // "find, then insert" done as two separate steps.
    // Two threads both miss the key and both insert it, and the duplicate survives one removal.
    // holds head.m for an O(n) walk, which also stalls readers at
    // the head — the hand-over-hand fine-grainedness buys you
    // nothing for this operation. That's not a mistake, it's a
    // fundamental truth: fine-grained locking gives you concurrency
    // for individual operations, but compound operations need coarse scope.
    template <typename Predicate>
    bool push_front_if_absent(Predicate p, const T& value)
    {
        // held for the WHOLE walk + insert
        std::lock_guard<std::mutex> lk(head.m);
        for (node* cur = head.next.get(); cur; cur = cur->next.get()) {
            if (p(*cur->data_ptr)) {
                // mutually exclusive with any reader copying this node
                std::lock_guard<std::mutex> node_lk(cur->m);
                *cur->data_ptr = value;
                return false;
            }
        }
        std::unique_ptr<node> new_node(std::make_unique<node>(value));
        new_node->next = std::move(head.next);
        head.next = std::move(new_node);
        return true;
    };

    template <typename Function>
    void for_each(Function f)
    {
        node* current = &head;
        std::unique_lock<std::mutex> lk(current->m);
        node* next = current->next.get();
        while (next) {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            f(*next->data_ptr);
            current = next;
            next = current->next.get();
            lk = std::move(next_lk);
        }
    };

    template <typename Predicate>
    std::shared_ptr<T> find_first_if(Predicate p)
    {
        node* current = &head;
        std::unique_lock<std::mutex> lk(current->m);
        node* next = current->next.get();
        while (next) {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            if (p(*next->data_ptr)) {
                return next->data_ptr;
            }
            current = next;
            next = current->next.get();
            lk = std::move(next_lk);
        }
        return std::shared_ptr<T>();
    };

    // Variant of find_first_if for entries that can be MUTATED IN PLACE
    // after publication (e.g. the lookup table's add_or_update_mapping
    // rewriting an existing entry's value).
    //
    // Why this exists: find_first_if returns the stored shared_ptr itself,
    // and every node lock is released by the time the caller dereferences
    // it. That is only safe when the pointee is immutable after the push
    // (the threadsafe_ptr_queue pattern). Once a writer can assign into
    // *data_ptr of a live node, a caller copying through the returned
    // pointer reads the object with NO lock held, while the writer may be
    // overwriting it at the same instant — a data race and a torn read
    // (e.g. a std::string whose first half is the old value and second
    // half the new one, which TSAN flags and TEST 2 of the lookup table
    // driver catches by inspection).
    //
    // The fix is to move the copy INSIDE the locked region: the new
    // shared_ptr is constructed from *next->data_ptr while next_lk (the
    // matching node's own mutex) is still held. Writers that update an
    // entry in place take that same node mutex before writing (see
    // push_front_if_absent), so the copy and the overwrite are mutually
    // exclusive — the reader always sees either the whole old value or
    // the whole new one, never a mixture.
    //
    // Lock-ordering note: the acquisition order here is head.m -> node_i
    // -> node_{i+1}, the same order push_front_if_absent uses
    // (head.m held, then the matched node's mutex) and the same order
    // for_each/remove_if use. A thread holding a node lock never waits on
    // head.m, so no lock cycle can form and the traversal cannot deadlock
    // against an updater.
    template <typename Predicate>
    std::shared_ptr<T> find_first_copy_if(Predicate p)
    {
        node* current = &head;
        std::unique_lock<std::mutex> lk(current->m);
        node* next = current->next.get();
        while (next) {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            if (p(*next->data_ptr)) {
                // copy-construct the returned value while still holding
                // this node's lock; next_lk is released only after the
                // copy is complete (on return, at the earliest)
                return std::make_shared<T>(*next->data_ptr);
            }
            current = next;
            next = current->next.get();
            lk = std::move(next_lk);
        }
        return std::shared_ptr<T>();
    };

    template <typename Predicate>
    void remove_if(Predicate p)
    {
        node* current = &head;
        std::unique_lock<std::mutex> lk(current->m);
        node* next = current->next.get();
        while (next) {
            std::unique_lock<std::mutex> next_lk(next->m);
            if (p(*next->data_ptr)) {
                // cant delete like this because need to unlock its mutex (next_lk) after
                // current->next = std::move(next->next);
                std::unique_ptr<node> old_node = std::move(current->next);
                current->next = std::move(next->next);
                next_lk.unlock();
            } else {
                lk.unlock();
                current = next;
                lk = std::move(next_lk);
            }
            next = current->next.get();
        }
    };

    ~threadsafe_list()
    {
        remove_if([](const T& val) { return true; });
    };
};