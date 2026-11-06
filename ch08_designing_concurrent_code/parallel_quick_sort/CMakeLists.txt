# Build task from 8.1.2 "Dividing data recursively" (Listing 8.1): a parallel
# Quicksort that pushes each lower chunk onto a stack of pending chunks, runs a
# fixed number of worker threads that sort chunks off that stack, and has a
# waiting thread sort chunks while it waits. Header-only template class, so
# this is an INTERFACE library (an include path, nothing compiled on its own).

add_library(parallel_quick_sort INTERFACE)
target_include_directories(parallel_quick_sort INTERFACE include)

add_executable(parallel_quick_sort_driver tests/parallel_quick_sort_driver.cpp)
target_link_libraries(parallel_quick_sort_driver PRIVATE parallel_quick_sort)
