# Repo structure & build setup

How this practice/library/test sandbox is organized, so the convention stays consistent as it grows past a handful of chapters. Written once, referenced later — not something to re-derive each time.

## Naming philosophy

Chapter folders keep their number (`ch02_managing_threads`, `ch04_synchronizing_concurrent_operations`) because a chapter is still a reasonable unit to browse in book order. But anything below chapter level — build-task folders, library/target names, and notes files — is named after what it *is*, not where it sits in the book (no `2.1`, no `4_1` prefix). The point: someone using `threadsafe_queue` or reading `basic-thread-management-notes.md` should be able to tell what it is without cross-referencing the book. The section number that originated it still lives inside the file (as a comment or heading), for traceability, just not in the name.

## Layout

```
C++_Concurrency_Practice/
  CMakeLists.txt                        # root — one add_subdirectory() per build task
  review-queue.csv                      # review schedule — edited only via tools/review.py
  tools/
    review.py                           # due / log / add / edit / drop / list / stats / lint
    verify.sh                           # build gate: TSAN + ASan/UBSan + 2-TU link check
  ch02_managing_threads/
    basic-thread-management-notes.md    # 2.1 — one file per section, created when its loop runs
    thread_guard/                       # 2.1's build task
      CMakeLists.txt
      include/thread_guard.hpp
      tests/thread_guard_driver.cpp
  ch04_synchronizing_concurrent_operations/
    waiting-for-an-event-or-condition-notes.md   # 4.1
    threadsafe_queue/                   # 4.1's build task
      CMakeLists.txt
      include/threadsafe_queue.hpp
      tests/threadsafe_queue_driver.cpp
  ch06_.../
    thread_safe_stack/                  # reuses ch04's threadsafe_queue target, see below
    lookup_table/
  ...
```

Two rules, not one uniform rule, on purpose:

- **Notes are chapter-scoped, one file per section, named after the section's content.** Created the first time that section actually goes through the retrieval-practice loop — not pre-created for the whole book. Most sections never get code, so there's no reason to fabricate a folder for them.
- **Code is section-scoped, but only for sections with a build task, and named after what's being built.** Check `concurrency-progress-ledger.md`'s Notes column — if a section says "build: ...", it gets its own subfolder inside its chapter folder, named for the thing (`thread_guard`, `threadsafe_queue`), not the section number. Everything else stays flat.

## Notes file convention

Each notes file is a running log for that section, appended to as the loop happens: quiz questions asked, answers with confidence tags, self-check results against the book, self-explanation (for build tasks), and review notes. The section number and title go at the top of the file itself, as a heading — not in the filename. It's a study journal, not load-bearing state. Section status lives in the ledger, and the review schedule lives in `review-queue.csv`, indexed by concept item.

**Answers are never overwritten.** A correction goes directly under the original answer as `Correction (YYYY-MM-DD): …`. The original wording is the record of what was actually believed, and a retest depends on it. (Git showed one answer rewritten after review, leaving a review that criticized text no longer in the file.)

Reviews follow the fixed template in `concurrency-learning-framework.md`: Warm-up / Quiz / Build / Queue updates / Fix list.

**Quiz question numbering restarts at 1 in every new notes file.** Never continue a running count from a previous section's file, even when the previous section ended mid-number (e.g. 4.2's file went to Q22 — 4.3's file still starts at Q1, not Q23). This was inconsistently applied once (4.3 wrongly continued from 4.2's count) before being written down here — the rule exists in writing now specifically so it doesn't depend on remembering what the last file did.

## CMake pattern

Most build tasks in this book are header-only template classes (`template<typename T> class threadsafe_queue`, etc.), so they're declared as `INTERFACE` libraries: no compiled object, just an include path. A non-template build task (like `thread_guard`, which is concrete, not generic) *could* be a compiled `STATIC` library instead, but if it's small, `INTERFACE` is still simpler and fine.

```cmake
# ch0X_chapter_name/task_name/CMakeLists.txt
add_library(task_name INTERFACE)
target_include_directories(task_name INTERFACE include)

add_executable(task_name_driver tests/task_name_driver.cpp)
target_link_libraries(task_name_driver PRIVATE task_name)
```

Root `CMakeLists.txt` stays a flat list — one `add_subdirectory()` per build task, added as they're built.

**Scaffolding a new build task.** When a section's triage calls for a build task, Claude creates the whole skeleton at spec-issuance time, before the user implements anything: the task folder with empty `include/` and `tests/` subfolders, the task's own `CMakeLists.txt` (populated per the pattern above, not left as a stub), empty (0-byte) `include/task_name.hpp` and `tests/task_name_driver.cpp` files for the user to fill in, and the corresponding `add_subdirectory(...)` line appended to the root `CMakeLists.txt`. The stub files stay genuinely empty — no include guards, no class shell, no signatures — since the interface itself is part of what's being retrieved closed-book, not just the body.

ThreadSanitizer is on by default for every CMake target (`ENABLE_TSAN` option in the root file, `-fsanitize=thread`).

**Review gate:** `tools/verify.sh <task>`. It compiles the driver independently of CMake (in a temp dir; `build/` is untouched) and prints one line:
- TSAN runs, under `setarch -R`
- ASan+UBSan runs
- a two-TU link check that catches missing `inline` (ODR violations)

About `setarch -R`: a sanitized binary run directly can die with `FATAL: ThreadSanitizer: unexpected memory mapping`. That's the ASLR conflict documented in `build-toolchain-guide.md` §1.8. Either `setarch $(uname -m) -R ./binary` or the host-wide `vm.mmap_rnd_bits=28` fixes it. F5 debugging is unaffected, since GDB disables ASLR.

Not every build task gets the same signal from TSAN. RAII/ownership builds like `thread_guard` are tested with exception-path and lifetime assertions rather than stress. TSAN is harmless there, just not the relevant signal.

## Reuse across sections

When the framework calls for a later section to extend or reuse an earlier build (per its own "known build-and-test checkpoints" — e.g. 6.2/6.3 and 9.1 reusing 4.1's queue), the later `CMakeLists.txt` just links against the earlier target directly, by name:

```cmake
# ch09_.../thread_pool/CMakeLists.txt
add_executable(thread_pool_driver tests/thread_pool_driver.cpp)
target_link_libraries(thread_pool_driver PRIVATE threadsafe_queue)   # ch04's target
```

This only resolves if root's `add_subdirectory(ch04_.../threadsafe_queue)` line appears before `ch09_.../thread_pool`. No copy-pasting code between chapters — the build graph documents the reuse relationship, and target names being content-based (not section-numbered) makes that link read naturally.

## Naming

- Chapter folders: zero-padded number + descriptive slug — `ch02_managing_threads`, not `ch2` or `ch02`.
- Build-task folders, library targets, executable targets, and their source files: descriptive name only, no section number — `thread_guard`, `threadsafe_queue`, `threadsafe_queue_driver.cpp`. Underscores, not dots (CMake target names shouldn't contain dots).
- Notes files: descriptive name only, no section number, kebab-case, `-notes.md` suffix — `basic-thread-management-notes.md`.

## What's tracked in git

**Tracked:**
- this file
- `concurrency-learning-framework.md`
- `concurrency-progress-ledger.md`
- `review-queue.csv`
- `tools/`
- `session-prompts.md`
- every notes file, all code and CMake files, and the `.vscode`/format config

**Ignored** (`.gitignore`):
- the personal/reference docs: `build-toolchain-guide.md`, `concurrency-debugging-guide.md`, `.gitignore` itself
- build artifacts

Check with `git ls-files` or `git check-ignore -v <file>` rather than trusting this list if it matters. In the Cowork sandbox, use `git --no-optional-locks`.

## Related documents

- `concurrency-learning-framework.md` — the pedagogical system (triage, spacing, mastery criterion) this repo exists to execute.
- `concurrency-progress-ledger.md` — coverage table (section status, notes file, build target) and the fix list.
- `review-queue.csv` + `tools/review.py` — the review schedule, per concept item.
