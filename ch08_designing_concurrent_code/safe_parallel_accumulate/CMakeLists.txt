# Build task from 8.4.1 "Exception safety in parallel algorithms" (Listings
# 8.3-8.4): an exception-safe parallel accumulate. Each block runs as a
# std::packaged_task, so its result or exception comes back through a future,
# and a join_threads object joins every thread however the function exits.
# A bounded extension of the 2.4 parallel_accumulate build, kept as its own
# target. join_threads is reused by the 9.1 thread pool. Header-only, so this
# is an INTERFACE library (an include path, nothing compiled on its own).

add_library(safe_parallel_accumulate INTERFACE)
target_include_directories(safe_parallel_accumulate INTERFACE include)

add_executable(safe_parallel_accumulate_driver tests/safe_parallel_accumulate_driver.cpp)
target_link_libraries(safe_parallel_accumulate_driver PRIVATE safe_parallel_accumulate)
