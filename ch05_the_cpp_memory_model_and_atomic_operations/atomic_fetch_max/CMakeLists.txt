# Build task from 5.2.3 "Operations on std::atomic<bool>" — atomic_fetch_max,
# the compare/exchange update loop ("new value depends on the current value")
# applied to a lock-free fetch-max. Motivated by A/B feed arbitration.
# Function template, so header-only INTERFACE library.

add_library(atomic_fetch_max INTERFACE)
target_include_directories(atomic_fetch_max INTERFACE include)

add_executable(atomic_fetch_max_driver tests/atomic_fetch_max_driver.cpp)
target_link_libraries(atomic_fetch_max_driver PRIVATE atomic_fetch_max)
