#pragma once
#include <future>
#include <map>
#include <mutex>

// pending_requests — build task for section 4.2.3 (Making (std::)promises).
//
// Interface, thread-safety contract, and edge cases: see
// ch04_synchronizing_concurrent_operations/waiting-for-one-off-events-with-futures-notes.md
// for the full build spec.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// class yourself, per the framework's rule that Claude never writes the
// implementation.

template <typename Key, typename Value>
class pending_requests {
private:
    // mutex-protected map<Key, std::promise<Value>>. fulfill() should
    // remove the entry once resolved -- a promise is one-shot, same as a
    // packaged_task.
    std::mutex mut;
    std::map<Key, std::promise<Value>> id_promise_map;
    // i thought about having an internal thread along with
    // a condition variable, however, there is still gonna have
    // to be one thread blocking using a while loop - just not this
    // one if we use condition_variable but still one will have to
    // because like we said, this case is where we dont control when
    // an event is gonna happen.

public:
    pending_requests()
        : mut()
        , id_promise_map()
    {
    }
    // Registers a new outstanding request under `id`, returns the future
    // the caller will block on for the eventual response. Safe to call
    // concurrently from multiple caller threads registering different ids.
    std::future<Value> register_request(const Key& id)
    {
        // we create a future and move it into the map after
        // locking the map here, easy
        std::lock_guard<std::mutex> lk(mut);
        // make the promise and get the future out:
        std::promise<Value> p;
        std::future<Value> f = p.get_future();
        // now insert
        id_promise_map[id] = std::move(p);
        // return the future
        return f;
    };

    // Called by the (single, in this build's scope) receiving side
    // once a response for `id` has arrived. Makes
    // register_request()'s returned future ready with `value`.
    void fulfill(const Key& id, Value value)
    {
        // although this is gonna be supposedly run by a single thread
        // and thus doesnt need a mutex for protecting the map because
        // however for the case that it does end up getting called by
        // multiple threads as a thread pool responsible for doing this
        // and also not to mention that we are gonna need to remove the
        // promise from the map here, i think it should be protected
        // by a mutex though I also think we should keep protected lines
        // to a minimum.
        std::lock_guard<std::mutex> lk(mut);
        // now as a first we need to find the map entry against this
        // key
        // typename needed: Key/Value are template params, so std::map<Key, std::promise<Value>>
        // is a dependent type — the compiler won't assume ::iterator names a type without it.
        typename std::map<Key, std::promise<Value>>::iterator it
            = id_promise_map.find(id);

        //  with that we need to check if we found an entry and if not we
        // return
        if (it == id_promise_map.end()) {
            return;
        }
        // here we mean we did get an entry and so we can now access the promise
        // and set the value:
        it->second.set_value(value);
        // promises are one shot so we erase them as told in the contract.
        id_promise_map.erase(it);
        return;
    };
};
