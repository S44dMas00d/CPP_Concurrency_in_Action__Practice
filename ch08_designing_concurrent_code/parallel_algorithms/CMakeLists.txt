# Build task from 8.5 "Designing concurrent code in practice" (Listings
# 8.7-8.13): parallel for_each, find and partial_sum, each in an up-front-division
# version and a std::async (or barrier-based) version, plus the barrier class.
# One header per listing, all in namespace par. Header-only, so this is an
# INTERFACE library. Links safe_parallel_accumulate for join_threads.hpp (8.4).

add_library(parallel_algorithms INTERFACE)
target_include_directories(parallel_algorithms INTERFACE include)
target_link_libraries(parallel_algorithms INTERFACE safe_parallel_accumulate)

add_executable(parallel_algorithms_driver tests/parallel_algorithms_driver.cpp)
target_link_libraries(parallel_algorithms_driver PRIVATE parallel_algorithms)
