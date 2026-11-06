# Build task from 7.2.4 "Detecting nodes in use with reference counting"
# (Listings 7.10-7.11): a lock-free stack whose nodes are reclaimed with split
# reference counts (an external count kept with the pointer in head, an
# internal count in the node). Header-only template class, so INTERFACE library.
#
# std::atomic<counted_node_ptr> is 16 bytes (int + pointer). GCC implements
# 16-byte atomics as libatomic calls, so anything using this target must link
# libatomic. On GCC it also reports is_lock_free() == false: see the 7.2.4
# worksheet, Q3.

add_library(ref_counted_stack INTERFACE)
target_include_directories(ref_counted_stack INTERFACE include)
target_link_libraries(ref_counted_stack INTERFACE atomic)

add_executable(ref_counted_stack_driver tests/ref_counted_stack_driver.cpp)
target_link_libraries(ref_counted_stack_driver PRIVATE ref_counted_stack)
