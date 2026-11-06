#pragma once
#include <algorithm>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <vector>

// async_exchange_fetch — build task for section 4.2.1 (Returning values
// from background tasks).
//
// Interface, contract, and edge cases: see
// ch04_synchronizing_concurrent_operations/waiting-for-one-off-events-with-futures-notes.md
// for the full build spec.
//
// Deliberately empty beyond the includes you'll likely need. Write the
// function(s) yourself, per the framework's rule that Claude never writes
// the implementation.

extern std::map<std::string, double> exchange_price_map;

// Stand-in for a slow network call to one exchange. Simulate the latency
// with a sleep_for of a consistent, meaningful duration (e.g. 100-200ms)
// so timing differences between launch policies are actually observable,
// not lost in noise.
inline double fetch_price(std::string const& exchange_name)
{
    std::map<std::string, double>::iterator it = exchange_price_map.find(exchange_name);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (it != exchange_price_map.end()) {
        return it->second;
    }
    return -1.0;
}

// Your job: fire off one std::async per exchange with an explicit launch
// policy, collect every result via get(), and return them mapped back to
// the exchange that produced them (order of completion isn't the same as
// order of the input list, once these genuinely run concurrently).
inline std::vector<double> fetch_all_prices(std::vector<std::string> const& exchanges, std::launch policy)
{
    std::vector<double> prices(exchanges.size());
    std::vector<std::future<double>> prices_futures(exchanges.size());

    for (size_t i = 0; i < exchanges.size(); i++) {
        prices_futures[i] = std::async(policy, fetch_price, std::ref(exchanges[i]));
    }

    for (size_t i = 0; i < prices_futures.size(); i++) {
        prices[i] = prices_futures[i].get();
    }
    return prices;
}
