# ch04_synchronizing_concurrent_operations/async_exchange_fetch/CMakeLists.txt
# Build task for section 4.2.1 (Returning values from background tasks).

add_library(async_exchange_fetch INTERFACE)
target_include_directories(async_exchange_fetch INTERFACE include)

add_executable(async_exchange_fetch_driver tests/async_exchange_fetch_driver.cpp)
target_link_libraries(async_exchange_fetch_driver PRIVATE async_exchange_fetch)
