# Build task from 7.2.1-7.2.2 "Writing a thread-safe stack without locks" and
# "Stopping those pesky leaks" (Listings 7.2-7.5): a lock-free stack (CAS on
# head) with node reclamation by counting the threads in pop() and a pending
# "to be deleted" list. Header-only template class, so INTERFACE library.
# Later 7.2 sections (hazard pointers, reference counting, relaxed orders)
# start from this design.

add_library(lock_free_stack INTERFACE)
target_include_directories(lock_free_stack INTERFACE include)

add_executable(lock_free_stack_driver tests/lock_free_stack_driver.cpp)
target_link_libraries(lock_free_stack_driver PRIVATE lock_free_stack)
