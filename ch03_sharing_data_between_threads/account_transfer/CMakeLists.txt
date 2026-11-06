# Build task from 3.2.4 "Deadlock: the problem and a solution" — a small,
# focused exercise in the std::lock()+std::adopt_lock idiom (listing 3.6),
# applied to an atomic balance transfer between two mutex-protected accounts.
# Not a template (concrete class + free function), so like thread_guard this
# *could* be a compiled STATIC library, but it's small enough that
# header-only (INTERFACE) is simpler and stays consistent with the rest of
# this repo. Out-of-class member definitions must be `inline` (same ODR
# lesson as thread_guard, section 2.1) to avoid multiple-definition errors
# once more than one translation unit includes the header.

add_library(account_transfer INTERFACE)
target_include_directories(account_transfer INTERFACE include)

add_executable(account_transfer_driver tests/account_transfer_driver.cpp)
target_link_libraries(account_transfer_driver PRIVATE account_transfer)
