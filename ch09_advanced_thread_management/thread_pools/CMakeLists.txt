# Build task from 9.1 "Thread pools" (Listings 9.1-9.8): a simple pool, a pool
# with waitable tasks (function_wrapper + packaged_task futures) and
# run_pending_task(), parallel accumulate and Quicksort on that pool, a pool
# with thread-local work queues, and a work-stealing pool with its lock-based
# work_stealing_queue. One header per listing, all in namespace tp.
# Header-only, so an INTERFACE library. Reuses the 6.2.2 threadsafe_ptr_queue
# (its try_pop(T&) moves, so it holds move-only tasks) and 8.4's join_threads.

add_library(thread_pools INTERFACE)
target_include_directories(thread_pools INTERFACE include)
target_link_libraries(thread_pools INTERFACE threadsafe_ptr_queue safe_parallel_accumulate)

add_executable(thread_pools_driver tests/thread_pools_driver.cpp)
target_link_libraries(thread_pools_driver PRIVATE thread_pools)
