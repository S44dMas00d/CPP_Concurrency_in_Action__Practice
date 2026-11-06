#include "account_transfer.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
// Driver for the account_transfer build task (section 3.2.4).
//
// Requirements from the build spec:
//  - Self-transfer (transfer(acc, acc, amount)) must not double-lock the
//    same mutex (UB) and should leave the balance unchanged.
//  - The real test: many threads, many accounts, each thread repeatedly
//    calling transfer() between randomly chosen account pairs -- sometimes
//    (A, B), sometimes (B, A) for the same pair, from different threads,
//    concurrently. Must never deadlock (the run has to actually terminate),
//    and the sum of every account's balance must be exactly conserved
//    before vs. after.
//  - balance() needs its own lock -- don't assume it's safe just because
//    transfer() is correct.
//  - Must be ThreadSanitizer-clean (enabled by default via the root
//    CMakeLists' ENABLE_TSAN option -- no extra flags needed).

// C++ does not allow named function definitions inside another function definition.
void random_transfer(account& A, account& B)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::bernoulli_distribution bool_dist(0.5);
    std::uniform_int_distribution<int> int_dist(1, 100);

    for (int i = 0; i < 50; ++i) {
        bool b = bool_dist(gen);
        int amnt = int_dist(gen);

        if (b)
            // transfer(A, B, amnt);
            transfer_2(A, B, amnt);
        else
            // transfer(B, A, amnt);
            transfer_2(B, A, amnt);
    }
}

int main()
{
    // self-transfer check + concurrent random-pair transfer stress
    // test + balance-conservation assertion.

    account acctA(2000);
    account acctB(3000);

    // TEST 1 - Testing self-transfer guard.
    // transfer(acctA, acctA, 1000);
    transfer_2(acctA, acctA, 1000);
    assert(acctA.balance() == 2000 && "acct A should still have the same amount as it was instantiated with");
    // TEST 2 - the real test of threads calling the function
    // transfer concurrently. we will make two threads and give
    // them a predefined function this time that will select random
    // amount between 1 and 100 dollars and we do this process 50 times
    // for each thread.

    int cumulative_amount
        = acctA.balance() + acctB.balance();
    // by default the threads passes arguments by value but
    // the function expects them by reference and so we need to
    // use std::ref.
    std::thread t1(random_transfer, std::ref(acctA), std::ref(acctB));
    std::thread t2(random_transfer, std::ref(acctA), std::ref(acctB));
    t1.join();
    t2.join();
    assert((acctA.balance() + acctB.balance()) == cumulative_amount
        && "cumulative balance needs to be the same after random transactions.");

    std::cout << "All Tests Passed" << std::endl;
    return 0;
}
