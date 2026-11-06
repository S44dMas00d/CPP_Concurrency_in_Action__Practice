# Build task from 6.2.2 "A thread-safe queue using locks and condition
# variables" (Listing 6.3): a condvar-based queue that stores std::shared_ptr<T>.
# Header-only template class, so this is an INTERFACE library (an include path,
# nothing compiled on its own). A separate target from ch04's threadsafe_queue,
# which stays as reviewed. The 9.1 thread pool is expected to reuse this one.

add_library(threadsafe_ptr_queue INTERFACE)
target_include_directories(threadsafe_ptr_queue INTERFACE include)

add_executable(threadsafe_ptr_queue_driver tests/threadsafe_ptr_queue_driver.cpp)
target_link_libraries(threadsafe_ptr_queue_driver PRIVATE threadsafe_ptr_queue)
