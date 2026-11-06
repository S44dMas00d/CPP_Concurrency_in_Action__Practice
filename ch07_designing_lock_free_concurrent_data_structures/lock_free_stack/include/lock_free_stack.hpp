#pragma once
#include <atomic>
#include <memory>
#include <thread>

template <typename T>
class lock_free_stack {
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
    std::atomic<int> num_threads_in_pop;
    std::atomic<node*> to_be_deleted;

public:
    lock_free_stack()
    {
        head.store(nullptr);
        num_threads_in_pop.store(0);
        to_be_deleted.store(nullptr);
    }

    ~lock_free_stack()
    {
        delete_nodes(head.load());
        delete_nodes(to_be_deleted.load());
    }

    void push(const T& data)
    {
        node* new_node = new node(data);
        new_node->next = head.load();
        while (!head.compare_exchange_strong(new_node->next, new_node)) { };
    }

    void delete_nodes(node* nodes)
    {
        while (nodes) {
            node* next = nodes->next;
            delete nodes;
            nodes = next;
        }
    }

    void chain_pending_nodes(node* first, node* last)
    {
        last->next = to_be_deleted.load();
        while (!to_be_deleted.compare_exchange_strong(last->next, first)) { };
    }

    void chain_pending_nodes(node* nodes)
    {
        node* last = nodes;
        while (node* next = last->next) {
            last = next;
        }
        chain_pending_nodes(nodes, last);
    }

    void chain_pending_node(node* n)
    {
        chain_pending_nodes(n, n);
    }

    void try_reclaim(node* old_head)
    {
        if (num_threads_in_pop == 1) {
            node* nodes_to_delete = to_be_deleted.exchange(nullptr);
            if (!--num_threads_in_pop) {
                delete_nodes(nodes_to_delete);
            } else if (nodes_to_delete) {
                chain_pending_nodes(nodes_to_delete);
            }
            delete old_head;
        } else {
            chain_pending_node(old_head);
            --num_threads_in_pop;
        }
    }

    std::shared_ptr<T> pop()
    {
        ++num_threads_in_pop;
        node* old_head = head.load();
        while (old_head && !head.compare_exchange_strong(old_head, old_head->next)) { };
        std::shared_ptr<T> res;
        if (old_head) {
            res.swap(old_head->data);
            try_reclaim(old_head);
        } else {
            --num_threads_in_pop;
        }
        return res;
    }
};