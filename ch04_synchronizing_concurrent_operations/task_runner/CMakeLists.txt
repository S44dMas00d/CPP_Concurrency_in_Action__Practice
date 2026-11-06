# Build task from 4.2.2 "Associating a task with a future" — a single
# background worker thread that executes posted std::packaged_task<void()>
# instances one at a time, each post_task() call returning a std::future<void>
# for that task's completion. Header-only class, so this is an INTERFACE
# library (an include path, nothing compiled on its own). A minimal building
# block toward ch9's thread pool.

add_library(task_runner INTERFACE)
target_include_directories(task_runner INTERFACE include)

add_executable(task_runner_driver tests/task_runner_driver.cpp)
target_link_libraries(task_runner_driver PRIVATE task_runner)
