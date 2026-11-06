# Build task from 3.2.3 "Spotting race conditions inherent in interfaces" —
# a race-free thread-safe stack (listings 3.4/3.5). Header-only template
# class, so this is an INTERFACE library (an include path, nothing compiled
# on its own). Originally parked as a future ch6.2 build in the ledger; moved
# up to here since the book fully derives and implements it in this
# subsection. ch6.2 will extend/interleave against this target rather than
# building a fresh one.

add_library(threadsafe_stack INTERFACE)
target_include_directories(threadsafe_stack INTERFACE include)

add_executable(threadsafe_stack_driver tests/threadsafe_stack_driver.cpp)
target_link_libraries(threadsafe_stack_driver PRIVATE threadsafe_stack scoped_thread)
