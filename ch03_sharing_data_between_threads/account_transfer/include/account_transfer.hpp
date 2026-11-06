#pragma once
#include <mutex>
#include <utility>

// account_transfer — build task for section 3.2.4 (Deadlock: the problem
// and a solution).
//
// Interface, contract, and edge cases: see
// ch03_sharing_data_between_threads/protecting-shared-data-with-mutexes-notes.md.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// class and function yourself, per the framework's rule that Claude never
// writes the implementation.
//
// Reminder: this header will be included by multiple translation units once
// linked into a real build. Any out-of-class member function definitions
// you write here must be `inline` — same ODR lesson from thread_guard
// (section 2.1) applies.

class account {
private:
    mutable std::mutex mt;
    int bal;

public:
    explicit account(int initial_balance)
        : bal(initial_balance) {};
    int balance() const;
    std::pair<int, std::unique_lock<std::mutex>>
    balance_2(std::unique_lock<std::mutex>&& u_lk) const;
    // using adopt_lock + lock + lock_guard
    friend void transfer(account& from, account& to, int amount);
    // using defer_lock + lock + unique_lock
    friend void transfer_2(account& from, account& to, int amount);
};

int account::balance() const
{
    std::lock_guard<std::mutex> lk(mt);
    return bal;
}

std::pair<int, std::unique_lock<std::mutex>>
account::balance_2(std::unique_lock<std::mutex>&& u_lk) const
{
    return { bal, std::move(u_lk) };
}

void transfer(account& from, account& to, int amount)
{
    int from_balance = from.balance();
    // int to_balance = to.balance();

    // to avoid double locking same mutex
    if (&from == &to) {
        return;
    }
    std::lock(from.mt, to.mt);
    std::lock_guard<std::mutex> lk_from(from.mt, std::adopt_lock);
    std::lock_guard<std::mutex> lk_to(to.mt, std::adopt_lock);
    if (amount > from_balance) {
        return;
    }
    // now update the balances
    from.bal -= amount;
    to.bal += amount;
}

void transfer_2(account& from, account& to, int amount)
{
    // to avoid double locking same mutex
    if (&from == &to) {
        return;
    }
    std::unique_lock<std::mutex> lk_from(from.mt, std::defer_lock);
    std::unique_lock<std::mutex> lk_to(to.mt, std::defer_lock);
    std::lock(lk_from, lk_to);

    std::pair<int, std::unique_lock<std::mutex>> result = from.balance_2(std::move(lk_from));
    int from_balance = result.first;
    lk_from = std::move(result.second);

    // move funds checking so that the check
    if (amount > from_balance) {
        return;
    }
    // now update the balances
    from.bal -= amount;
    to.bal += amount;
}