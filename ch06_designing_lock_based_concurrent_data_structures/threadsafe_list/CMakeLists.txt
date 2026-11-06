# Build task from 6.3.2 "Writing a thread-safe list using locks" (Listing 6.13):
# a singly linked list with one mutex per node and hand-over-hand locking.
# Header-only template class, so this is an INTERFACE library.
# threadsafe_lookup_table (6.3.1) uses it for its buckets.

add_library(threadsafe_list INTERFACE)
target_include_directories(threadsafe_list INTERFACE include)

add_executable(threadsafe_list_driver tests/threadsafe_list_driver.cpp)
target_link_libraries(threadsafe_list_driver PRIVATE threadsafe_list)
