# Build task from 5.2.7 "Free functions for atomic operations": snapshot_cell,
# a read-mostly value published as an immutable snapshot through the
# std::shared_ptr atomic free functions (atomic_load / atomic_store /
# atomic_compare_exchange_*).

add_library(snapshot_cell INTERFACE)
target_include_directories(snapshot_cell INTERFACE include)

add_executable(snapshot_cell_driver tests/snapshot_cell_driver.cpp)
target_link_libraries(snapshot_cell_driver PRIVATE snapshot_cell)
