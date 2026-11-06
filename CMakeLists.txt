cmake_minimum_required(VERSION 3.16)

# Pin the native compiler explicitly. This machine also has an ARM bare-metal
# cross-compiler on PATH (<arm-toolchain-dir>/bin) for
# embedded work, and CMake will cache that instead of the native one if it gets
# picked up first, which breaks linking (no hosted libc, no pthreads).
#
# NOTE: this guard only covers a plain `cmake -S . -B build` (the "cmake build"
# shell task). It does NOT protect the CMake Tools path: a kit passes
# -DCMAKE_CXX_COMPILER=... on the command line, so the variable is already
# defined and this if() never fires. For that path the defence is picking the
# GCC kit in "CMake: Select a Kit".
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER /usr/bin/g++)
endif()
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER /usr/bin/gcc)
endif()

project(cpp_concurrency_practice CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_TSAN "Build with ThreadSanitizer" ON)
if(ENABLE_TSAN)
  # -O1 is the documented TSAN compromise, not a debugging choice: -O0 makes
  # sanitized builds punishingly slow, and higher optimization damages TSAN's
  # own stack traces. Cost: at -O1 the compiler still freely reorders code and
  # elides short-lived locals, so GDB can show stale line hits and "optimized
  # out" for values you want to inspect. Use the "debug" variant below (or the
  # `cmake build (no TSAN)` task) when you actually need to step through logic.
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
else()
  # No sanitizer instrumentation, so no reason to hold to -O1 — -O0 keeps
  # every local variable live and every line breakpoint-able, at the cost of
  # not catching races. This is the debugger-friendly configuration.
  add_compile_options(-g -O0)
endif()

# One line per build task, added in book order as they're built.
# Later sections may link against an earlier section's target when the
# framework calls for reuse (e.g. 6.2/6.3 and 9.1 reusing 4.1's queue) —
# see repo-structure.md for the pattern. Folders are named after section
# content, not section numbers — see repo-structure.md's naming convention.
add_subdirectory(ch02_managing_threads/thread_guard)
add_subdirectory(ch02_managing_threads/scoped_thread)
add_subdirectory(ch02_managing_threads/parallel_accumulate)
add_subdirectory(ch03_sharing_data_between_threads/threadsafe_stack)
add_subdirectory(ch03_sharing_data_between_threads/account_transfer)
add_subdirectory(ch03_sharing_data_between_threads/lazy_shared_resource)
add_subdirectory(ch03_sharing_data_between_threads/shared_mutex_cache)
add_subdirectory(ch04_synchronizing_concurrent_operations/threadsafe_queue)
add_subdirectory(ch04_synchronizing_concurrent_operations/async_exchange_fetch)
add_subdirectory(ch04_synchronizing_concurrent_operations/task_runner)
add_subdirectory(ch04_synchronizing_concurrent_operations/pending_requests)
add_subdirectory(ch05_the_cpp_memory_model_and_atomic_operations/spinlock_mutex)
add_subdirectory(ch05_the_cpp_memory_model_and_atomic_operations/atomic_fetch_max)
add_subdirectory(ch05_the_cpp_memory_model_and_atomic_operations/snapshot_cell)
add_subdirectory(ch05_the_cpp_memory_model_and_atomic_operations/publish_once)
add_subdirectory(ch06_designing_lock_based_concurrent_data_structures/threadsafe_ptr_queue)
add_subdirectory(ch06_designing_lock_based_concurrent_data_structures/fine_grained_queue)
add_subdirectory(ch06_designing_lock_based_concurrent_data_structures/threadsafe_list)
add_subdirectory(ch06_designing_lock_based_concurrent_data_structures/threadsafe_lookup_table)
add_subdirectory(ch07_designing_lock_free_concurrent_data_structures/lock_free_stack)
add_subdirectory(ch07_designing_lock_free_concurrent_data_structures/hazard_pointer_stack)
add_subdirectory(ch07_designing_lock_free_concurrent_data_structures/ref_counted_stack)
add_subdirectory(ch08_designing_concurrent_code/parallel_quick_sort)
add_subdirectory(ch08_designing_concurrent_code/safe_parallel_accumulate)
add_subdirectory(ch08_designing_concurrent_code/parallel_algorithms)
add_subdirectory(ch09_advanced_thread_management/thread_pools)
