#pragma once
#include <atomic>
#include <memory>
#include <thread>

struct config {
    long fee;
    long limit;
};

template <typename T>
class snapshot_cell {
private:
    std::shared_ptr<const T> current;

public:
    // initial must not be null

    explicit snapshot_cell(std::shared_ptr<const T> initial)
    {
        // cant defend race in initialization parameter list.
        // race
        std::atomic_store(&current, initial);
    }
    // as already mentioned in the contract - non copyable and non movable
    snapshot_cell(snapshot_cell const& other) = delete;
    snapshot_cell& operator=(snapshot_cell const& other) = delete;
    snapshot_cell(snapshot_cell&& other) = delete;
    snapshot_cell& operator=(snapshot_cell&& other) = delete;

    // the load function:
    // as per contract, the load returns an immutable snapshot which
    // means a const qualifier at the end of the function sig.
    std::shared_ptr<const T> load() const
    {
        // race
        return std::atomic_load(&current);
    }

    // the store function:
    void store(std::shared_ptr<const T> next)
    {
        // race
        std::atomic_store(&current, next);
    }

    // the update function:
    // f takes const T& and returns a new T
    template <typename F>
    void update(F f)
    {
        // race
        std::shared_ptr<const T> expected = std::atomic_load(&current);
        // race
        while (true) {
            // the important part is that f receives the current T,
            // not the shared_ptr itself.
            // race
            T next = f(*expected);
            // race
            // For something containing a std::vector or std::string,
            // moving can avoid copying heap-managed data.
            // race
            std::shared_ptr<const T> desired
                = std::make_shared<const T>(std::move(next));
            // race
            if (std::atomic_compare_exchange_strong(
                    &current, &expected, desired)) {
                break;
            }
        }
    }
};
