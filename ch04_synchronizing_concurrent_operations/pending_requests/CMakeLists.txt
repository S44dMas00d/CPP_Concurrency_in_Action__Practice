# Build task from 4.2.3 "Making (std::)promises" -- a thread-safe table
# mapping a caller-assigned request id to a std::promise<Value>, generalizing
# Listing 4.10's single-thread connection-demux pattern. Header-only template
# class, so this is an INTERFACE library (an include path, nothing compiled
# on its own).

add_library(pending_requests INTERFACE)
target_include_directories(pending_requests INTERFACE include)

add_executable(pending_requests_driver tests/pending_requests_driver.cpp)
target_link_libraries(pending_requests_driver PRIVATE pending_requests)
