# Build task from 5.3.3 "Memory ordering for atomic operations": publish_once,
# a value written exactly once by whichever publisher wins, then read lock-free
# by any number of readers. Publication uses release/acquire ordering (no
# seq_cst). 

add_library(publish_once INTERFACE)
target_include_directories(publish_once INTERFACE include)

add_executable(publish_once_driver tests/publish_once_driver.cpp)
target_link_libraries(publish_once_driver PRIVATE publish_once)
