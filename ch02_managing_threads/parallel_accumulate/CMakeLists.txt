# Build task from 2.4 "Choosing the number of threads at runtime" —
# parallel_accumulate (listing 2.8). Template function, header-only, so this
# is an INTERFACE library same as the earlier build tasks.

add_library(parallel_accumulate INTERFACE)
target_include_directories(parallel_accumulate INTERFACE include)

add_executable(parallel_accumulate_driver tests/parallel_accumulate_driver.cpp)
target_link_libraries(parallel_accumulate_driver PRIVATE parallel_accumulate)
