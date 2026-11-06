# Build task from 6.3.1 "Writing a thread-safe lookup table using locks"
# (Listings 6.11-6.12): a fixed-bucket hash table. Unlike the book, each
# bucket is a threadsafe_list (6.3.2, Listing 6.13) instead of a std::list
# guarded by a shared_mutex. Header-only template class, so INTERFACE library.

add_library(threadsafe_lookup_table INTERFACE)
target_include_directories(threadsafe_lookup_table INTERFACE include)
target_link_libraries(threadsafe_lookup_table INTERFACE threadsafe_list)

add_executable(threadsafe_lookup_table_driver tests/threadsafe_lookup_table_driver.cpp)
target_link_libraries(threadsafe_lookup_table_driver PRIVATE threadsafe_lookup_table)
