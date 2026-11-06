# Build task from 4.1 "Waiting for an event or other condition" — condvar-based
# threadsafe_queue. Header-only template class, so this is an INTERFACE library
# (an include path, nothing compiled on its own). Later sections that reuse
# this queue (6.2/6.3, 9.1) link against the `threadsafe_queue` target directly.

add_library(threadsafe_queue INTERFACE)
target_include_directories(threadsafe_queue INTERFACE include)

add_executable(threadsafe_queue_driver tests/threadsafe_queue_driver.cpp)
target_link_libraries(threadsafe_queue_driver PRIVATE threadsafe_queue)
