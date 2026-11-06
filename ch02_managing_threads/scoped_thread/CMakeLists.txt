# Build task from 2.3 "Transferring ownership of a thread" — scoped_thread
# (listing 2.6). Not a template, small enough that INTERFACE (header-only)
# is simplest, same reasoning as thread_guard.

add_library(scoped_thread INTERFACE)
target_include_directories(scoped_thread INTERFACE include)

add_executable(scoped_thread_driver tests/scoped_thread_driver.cpp)
target_link_libraries(scoped_thread_driver PRIVATE scoped_thread)
