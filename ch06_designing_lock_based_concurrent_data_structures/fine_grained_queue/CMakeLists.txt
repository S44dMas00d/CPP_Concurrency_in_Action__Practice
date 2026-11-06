# Build task from 6.2.3 "A thread-safe queue using fine-grained locks and
# condition variables" (Listings 6.7-6.10): a singly linked list with a dummy
# node, separate head and tail mutexes, and condition-variable waiting.
# Header-only template class, so this is an INTERFACE library. Ch7's lock-free
# queue starts from this design.

add_library(fine_grained_queue INTERFACE)
target_include_directories(fine_grained_queue INTERFACE include)

add_executable(fine_grained_queue_driver tests/fine_grained_queue_driver.cpp)
target_link_libraries(fine_grained_queue_driver PRIVATE fine_grained_queue)
