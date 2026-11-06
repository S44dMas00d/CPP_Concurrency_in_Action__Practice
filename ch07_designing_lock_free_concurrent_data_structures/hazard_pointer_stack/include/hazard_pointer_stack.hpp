#pragma once
#include "hazard_pointers.hpp"
#include <atomic>
#include <memory>
#include <thread>

template <typename T>
class hazard_pointer_stack {
private:
    struct node {
        std::shared_ptr<T> data;
        node* next;
        node(const T& data_)
            : data(std::make_shared<T>(data_))
            , next(nullptr)
        {
        }
    };
    std::atomic<node*> head;

public:
    hazard_pointer_stack()
    {
        head.store(nullptr);
    };

    hazard_pointer_stack(const hazard_pointer_stack&) = delete;
    hazard_pointer_stack& operator=(const hazard_pointer_stack&) = delete;

    ~hazard_pointer_stack()
    {
        hazard_ptr::delete_nodes_with_no_hazards();
    };

    void push(const T& data)
    {
        node* new_node = new node(data);
        new_node->next = head.load();
        while (!head.compare_exchange_strong(new_node->next, new_node)) { };
    };

    std::shared_ptr<T> pop()
    {
        std::atomic<void*>& hp = hazard_ptr::get_hazard_pointer_for_current_thread();
        node* old_head = head.load();
        do {
            node* temp;
            do {
                temp = old_head;
                hp.store(old_head);
                old_head = head.load();
            } while (old_head != temp);

        } while (old_head && !head.compare_exchange_strong(old_head, old_head->next));
        hp.store(nullptr);
        std::shared_ptr<T> res;
        if (old_head) {
            res.swap(old_head->data);
            if (hazard_ptr::outstanding_hazard_pointers_for(old_head)) {
                hazard_ptr::reclaim_later(old_head);
            } else {
                delete old_head;
            }
            hazard_ptr::delete_nodes_with_no_hazards();
        }
        return res;
    };
};
