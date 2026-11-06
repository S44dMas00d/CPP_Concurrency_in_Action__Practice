# ch03_sharing_data_between_threads/shared_mutex_cache/CMakeLists.txt
# Build task for section 3.3.2 (Protecting rarely updated data structures).

add_library(shared_mutex_cache INTERFACE)
target_include_directories(shared_mutex_cache INTERFACE include)

add_executable(shared_mutex_cache_driver tests/shared_mutex_cache_driver.cpp)
target_link_libraries(shared_mutex_cache_driver PRIVATE shared_mutex_cache)
