#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

template <typename T>
class fine_grained_queue {
    // private:
public:
    // data members
    struct node {
        std::unique_ptr<node> next;
        std::shared_ptr<T> data_ptr;
    };
    std::unique_ptr<node> head;
    node* tail;

    std::mutex head_mutex;
    std::mutex tail_mutex;
    std::condition_variable data_cond;

public:
    // methods
    fine_grained_queue()
        : head(new node)
        , tail(head.get()) {};

    // ~fine_grained_queue() {
    //     // hold mutex to head_mutex and then keep popping off
    //     // have a while loop that runs until we have a situation
    //     // where tail and head point to the same dummy node.
    //     // then exit the while loop and delete the dummy node and
    //     // cleanup is done.
    // };

    void push(T new_value)
    {
        std::unique_ptr<node> p(new node);
        std::shared_ptr<T> new_data_ptr(std::make_shared<T>(std::move(new_value)));
        {
            std::lock_guard<std::mutex> tail_lock(tail_mutex);
            tail->data_ptr = new_data_ptr;
            tail->next = std::move(p);
            tail = tail->next.get();
        }
        data_cond.notify_one();
    };

    bool empty()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        return (head.get() == get_tail());
    };

    node* get_tail()
    {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        return tail;
    };

    void wait_and_pop(T& value)
    {
        std::unique_lock<std::mutex> head_lock(head_mutex);
        data_cond.wait(head_lock, [this]() { return get_tail() != head.get(); });
        value = *(head.get()->data_ptr);
        std::unique_ptr<node> old_head(std::move(head));
        head = std::move(old_head.get()->next);
        return;
    };

    std::shared_ptr<T> wait_and_pop()
    {
        std::unique_lock<std::mutex> head_lock(head_mutex);
        data_cond.wait(head_lock, [this]() { return get_tail() != head.get(); });
        std::unique_ptr<node> old_head(std::move(head));
        head = std::move(old_head.get()->next);
        return std::move(old_head.get()->data_ptr);
    };

    bool try_pop(T& value)
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        node* tail_ptr = nullptr;
        {
            std::lock_guard<std::mutex> tail_lock(tail_mutex);
            tail_ptr = tail;
        }
        if (head.get() == tail_ptr) {
            return false;
        }
        // Safer for preserving the queued value if assignment throws:
        // 1. Copy-assigns the queued T into value.
        // 2. The queued T remains unchanged by the copy itself.
        // 3. If assignment succeeds, pop the node below.
        // 4. If assignment throws, the node is not popped, although value
        //    may have been partially modified by T's copy-assignment operator.
        value = *(head.get()->data_ptr);

        // Alternative: move-assign instead.
        // 1. std::move casts the queued T to an rvalue.
        // 2. Move assignment may transfer resources into value, replacing
        //    its previous contents.
        // 3. The queued T may become moved-from before the node is popped.
        // 4. If move assignment throws, the node remains queued but its T
        //    may already be modified, potentially losing the original value.
        // value = std::move(*(head.get()->data_ptr));

        // Pop only after assignment succeeds:
        std::unique_ptr<node> old_head(std::move(head));
        head = std::move(old_head.get()->next);
        return true;
    };

    std::shared_ptr<T> try_pop()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        node* tail_ptr = nullptr;
        {
            std::lock_guard<std::mutex> tail_lock(tail_mutex);
            tail_ptr = tail;
        }
        if (head.get() == tail_ptr) {
            return nullptr;
        }
        std::unique_ptr<node> old_head(std::move(head));
        head = std::move(old_head.get()->next);
        // More efficient: move data_ptr out of the node.
        // Rough trace:
        // 1. std::move casts data_ptr to an rvalue.
        // 2. Return value move-constructs a shared_ptr (ownership transferred;
        //    no reference-count increment).
        // 3. old_head destroys the node; its now-empty data_ptr does nothing.
        //    The returned shared_ptr retains ownership of the data.
        return std::move(old_head.get()->data_ptr); // no-except for shared_ptrs

        // Alternative: copy data_ptr instead.
        // Rough trace:
        // 1. Copy-constructs the return value, incrementing the reference count.
        // 2. old_head destroys the node; its data_ptr decrements the count.
        // 3. The returned shared_ptr still owns the data, but the extra
        //    reference-count operations were unnecessary.
        // return old_head.get()->data_ptr; // no-except for shared_ptrs
    };
};
