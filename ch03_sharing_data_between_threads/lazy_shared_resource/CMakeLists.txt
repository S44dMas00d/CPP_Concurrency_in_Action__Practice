# ch03_sharing_data_between_threads/lazy_shared_resource/CMakeLists.txt
# Build task for section 3.3.1 (Protecting shared data during initialization).

add_library(lazy_shared_resource INTERFACE)
target_include_directories(lazy_shared_resource INTERFACE include)

add_executable(lazy_shared_resource_driver tests/lazy_shared_resource_driver.cpp)
target_link_libraries(lazy_shared_resource_driver PRIVATE lazy_shared_resource)
