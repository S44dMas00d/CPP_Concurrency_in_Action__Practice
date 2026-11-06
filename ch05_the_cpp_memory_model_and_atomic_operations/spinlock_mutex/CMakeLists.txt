# Build task from 5.2.2 "Operations on std::atomic_flag" — spinlock_mutex,
# a BasicLockable busy-wait mutex built from a single std::atomic_flag
# (Listing 5.1). Not a template, small enough that INTERFACE (header-only)
# is simplest, same reasoning as thread_guard/scoped_thread.

add_library(spinlock_mutex INTERFACE)
target_include_directories(spinlock_mutex INTERFACE include)

add_executable(spinlock_mutex_driver tests/spinlock_mutex_driver.cpp)
target_link_libraries(spinlock_mutex_driver PRIVATE spinlock_mutex)
