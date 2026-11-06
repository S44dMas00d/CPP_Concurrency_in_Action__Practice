# Build task from 7.2.3 "Detecting nodes that can't be reclaimed using hazard
# pointers" (Listings 7.6-7.8): a general-purpose hazard pointer utility
# (hazard_pointers.hpp: per-thread hazard pointer slots, deferred reclaim
# list) and a lock-free stack whose pop() uses it (hazard_pointer_stack.hpp).
# Header-only, so INTERFACE library. Separate from lock_free_stack (7.2.2),
# which stays as reviewed.

add_library(hazard_pointer_stack INTERFACE)
target_include_directories(hazard_pointer_stack INTERFACE include)

add_executable(hazard_pointer_stack_driver tests/hazard_pointer_stack_driver.cpp)
target_link_libraries(hazard_pointer_stack_driver PRIVATE hazard_pointer_stack)
