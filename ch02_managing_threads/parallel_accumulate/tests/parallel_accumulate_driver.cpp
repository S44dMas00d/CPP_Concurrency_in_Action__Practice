#include "parallel_accumulate.hpp"
#include <cassert>
#include <iostream>
#include <numeric>
#include <vector>

// Driver for the parallel_accumulate build task (section 2.4).
//
// Not a TSAN-hunting exercise the way earlier drivers were -- there's no
// shared mutable state between threads here if the decomposition is done
// right (see the framing question in the notes file). Still run under TSAN
// as a sanity check; a race here would mean a real bug (e.g. overlapping
// block boundaries).
//
// What actually matters: correctness against std::accumulate across a
// range of sizes -- empty, 1 element, fewer than your per-thread minimum,
// exactly divisible, not evenly divisible, and large enough to actually
// spawn multiple threads -- with real assertions, not just "didn't crash".
// Optional: compare against std::accumulate on a float/double range and
// just look at the result (don't assert equality -- see quiz Q2).

int main()
{
    // TODO: exercise the cases above.
    // EMPTY CASE:
    std::vector<int> v0(0);
    std::iota(v0.begin(), v0.end(), 0);
    int res0 = parallel_accumulate(v0.begin(), v0.end(), 0);
    int res0_compare = std::accumulate(v0.begin(), v0.end(), 0);
    assert(res0 == res0_compare && "result for empty integer vectors should be equal in both parallel and std::versions");

    // REGULAR CASE:
    // first we check a normal case of 300 values:
    std::vector<int> v1(300);
    std::iota(v1.begin(), v1.end(), 0);
    int res1 = parallel_accumulate(v1.begin(), v1.end(), 0);
    int res1_compare = std::accumulate(v1.begin(), v1.end(), 0);
    assert(res1 == res1_compare && "result for integer vectors should be equal in both parallel and std::versions");

    // ONE ELEMMENT CASE:
    std::vector<int> v2(1);
    std::iota(v2.begin(), v2.end(), 1);
    int res2 = parallel_accumulate(v2.begin(), v2.end(), 0);
    int res2_compare = std::accumulate(v2.begin(), v2.end(), 0);
    assert(res2 == res2_compare && "result for integer vectors with one element should be equal in both parallel and std::versions");

    // LESS THAN MINIMUM ELEMENTS BUT MORE THAN 1
    std::vector<int> v3(30);
    std::iota(v3.begin(), v3.end(), 1);
    int res3 = parallel_accumulate(v3.begin(), v3.end(), 0);
    int res3_compare = std::accumulate(v3.begin(), v3.end(), 0);
    assert(res3 == res3_compare && "result for integer vectors should be equal in both parallel and std::versions");

    // NOT EVENLY DIVISIBLE:
    std::vector<int> v4(939);
    std::iota(v4.begin(), v4.end(), 1);
    int res4 = parallel_accumulate(v4.begin(), v4.end(), 0);
    int res4_compare = std::accumulate(v4.begin(), v4.end(), 0);
    assert(res4 == res4_compare && "result for integer vectors should be equal in both parallel and std::versions");

    // OPTIONAL FLOAT CASE:
    std::vector<double> v5(300);
    std::iota(v5.begin(), v5.end(), 0.1111);
    double res5 = parallel_accumulate(v5.begin(), v5.end(), 0);
    double res5_compare = std::accumulate(v5.begin(), v5.end(), 0);
    // assert(res5 == res5_compare && "result for integer vectors should be equal in both parallel and std::versions");
    // OBSERVATION - somehow there was no difference for either doubles or floats when i tested the functions.
    return 0;
}
