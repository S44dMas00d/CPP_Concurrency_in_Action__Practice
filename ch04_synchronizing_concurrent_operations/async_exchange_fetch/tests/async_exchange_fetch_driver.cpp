#include "async_exchange_fetch.hpp"
#include <algorithm>
#include <cassert>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

// Driver for the async_exchange_fetch build task (section 4.2.1).
//
// Requirements from the build spec -- see
// ch04_synchronizing_concurrent_operations/waiting-for-one-off-events-with-futures-notes.md
// for full detail:
//  - std::launch::async genuinely overlaps in time -- prove it with
//    timestamps around fetch_all_prices() across several simulated
//    exchanges, each with consistent, meaningful simulated latency.
//  - std::launch::deferred does not -- same function, same latency,
//    total time should land close to the sum instead. Reuse the same
//    timing harness as the async case, just swap the launch policy.
//  - Results map back to the right exchange regardless of completion order.
//  - Somewhere separate: concretely demonstrate listing 4.7's f1-vs-f2
//    pointer-vs-copy distinction (mutating a real object via std::async
//    through a pointer vs. through a copy).

std::map<std::string, double> exchange_price_map = {
    { "venue_a", 74500.10 },
    { "venue_b", 75500.10 },
    { "venue_c", 75000.65 },
    { "venue_d", 76500.10 },
    { "venue_e", 77000.10 },
};

int main()
{
    // write the actual driver yourself, closed-book.
    // TEST#1:
    // std::launch::async genuinely overlaps in time. Time fetch_all_prices()
    // across, say, 5 exchanges each simulating ~150ms of latency, using
    // std::launch::async explicitly. Total wall-clock time should land close
    // to ~150ms, not ~750ms — prove it with timestamps, the same style of proof
    // you already built for wait_and_pop() in 4.1.

    // manually writing keys in vector to ensure order
    const std::vector<std::string> exchanges = { "venue_a", "venue_b", "venue_c", "venue_d", "venue_e" };
    auto start_1 = std::chrono::steady_clock::now();
    std::vector<double> prices = fetch_all_prices(exchanges, std::launch::async);
    auto stop_1 = std::chrono::steady_clock::now();
    auto elapsed_time_1 = std::chrono::duration_cast<std::chrono::milliseconds>(stop_1 - start_1);
    std::cout << "time elapsed with async policy = " << elapsed_time_1.count() << std::endl;
    assert(elapsed_time_1 < std::chrono::milliseconds(300) && "with the std:::launch::async policy, the 5 runs should all complete around"
        && "200 ms concurrently, so total execution should be below 300 ms");

    // TEST#2:
    // std::launch::deferred does not — same function, same simulated
    // latency, but with std::launch::deferred instead. Since each
    // deferred task only actually runs the moment its own future's
    // get() is called, and you're calling get() on each in sequence,
    // total time should land close to the sum (~750ms), not the
    // concurrent figure from #1. This is the direct, empirical version
    // of Q7's gotcha — reuses the same timing harness from #1, just
    // swapping the launch policy, so this shouldn't cost you a second
    // harness to build.

    auto start_2 = std::chrono::steady_clock::now();
    prices = fetch_all_prices(exchanges, std::launch::deferred);
    auto stop_2 = std::chrono::steady_clock::now();
    auto elapsed_time_2 = std::chrono::duration_cast<std::chrono::milliseconds>(stop_2 - start_2);
    std::cout << "time elapsed with deferred policy = " << elapsed_time_2.count() << std::endl;
    assert(elapsed_time_2 > std::chrono::milliseconds(700) && "with the std:::launch::deffered policy, the 5 runs should all "
        && "start when blocked at get() and since get is called in loop, so execution is serialized"
        && "so total execution should be above 700 ms at least");

    // TEST#3:
    // Results map back to the right exchange, even though completion
    // order under real concurrency isn't guaranteed to match input order.
    bool prices_are_in_order_with_exchanges = true;
    for (size_t i = 0; i < prices.size(); i++) {
        std::string exchange = exchanges[i];
        if (exchange_price_map.find(exchange)->second != prices[i]) {
            prices_are_in_order_with_exchanges = false;
            break;
        }
    }
    assert(prices_are_in_order_with_exchanges && "Results should map back to the right exchange, even though completion order under real concurrency isn't guaranteed to match input order.");

    // Somewhere in the driver (a small, separate check, not a big new
    // harness): concretely demonstrate listing 4.7's f1-vs-f2
    // distinction from Q5 — call an async member function once via a
    // pointer to a real object and once via a copy, and show the
    // original object is mutated in one case and isn't in the other.

    struct X {
        int count;
        X()
            : count(0) {};
        void incrementor(int&& num)
        {
            count += num;
        };
    };

    X x1;
    X x2;
    std::future<void> res1 = std::async(&X::incrementor, &x1, 12);
    res1.wait();
    std::future<void> res2 = std::async(&X::incrementor, x2, 24);
    res2.wait();

    assert(x1.count == 12 && "passed this object reference along with the function pointer, update to count should register");
    assert(x2.count != 24 && "passed this object by value along with the function pointer, update to count should NOT register");

    std::cout << "All Tests Passed" << std::endl;
    return 0;
}
