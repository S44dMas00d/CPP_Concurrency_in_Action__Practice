# Build task from 2.1 "Basic thread management" — thread_guard, an RAII join
# wrapper (listing 2.3). Not a template (works with std::thread specifically),
# so unlike threadsafe_queue this *could* be a compiled STATIC library — but
# it's small enough that header-only (INTERFACE) is simpler and still fine.

add_library(thread_guard INTERFACE)
target_include_directories(thread_guard INTERFACE include)

add_executable(thread_guard_driver tests/thread_guard_driver.cpp)
target_link_libraries(thread_guard_driver PRIVATE thread_guard)
