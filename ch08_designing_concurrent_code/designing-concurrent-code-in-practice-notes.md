# 8.5 — Designing concurrent code in practice

## 8.5.1 (parallel for_each, Listings 8.7–8.8), 8.5.2 (parallel find, Listings 8.9–8.10), 8.5.3 (parallel partial_sum and a barrier, Listings 8.11–8.13)

**Build-only worksheet (your decision, 2026-11-01).** 8.5 is worked code examples, so this sheet has build tasks for Listings 8.7–8.13 and no warm-ups or quiz.
- The warm-up items stay due, and come back on the next normal worksheet. Nothing is logged for this one.
- Tests are deliberately few: one or two per listing, checking that it works, not every edge case.
- The review will still queue 2–4 `core` items when 8.5 closes.

```
$ python3 tools/review.py due   (2026-11-01; summary only, warm-ups not asked this sheet)
Due now: 114 active (110 overdue) | maintenance due: 1 | due in next 3 days: 6 | total active: 120, retired: 1
Open fix-list items: 19 (F1-F19)
```

Triage:
- **Every listing is a reusable mechanism, so all of them are builds:** one target, `parallel_algorithms`, with one header per listing. The barrier in 8.12 and its 8.13 extension share one header.
- **Not built:** 8.5's intro and the "find the photograph" scene-setting, and the O(N) vs O(N log N) comparison in 8.5.3.

### Build task — `parallel_algorithms` (Listings 8.7–8.13)

Folder: `ch08_designing_concurrent_code/parallel_algorithms/`. It's scaffolded:
- `CMakeLists.txt`, which links `safe_parallel_accumulate` so `#include "join_threads.hpp"` works;
- the root `add_subdirectory` line;
- seven empty headers and one empty driver.

Book closed.

Interface (everything in `namespace par`, so nothing clashes with your earlier headers, as F18 and F19 did):

```cpp
// parallel_for_each.hpp     (Listing 8.7)
template<typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func f);

// async_for_each.hpp        (Listing 8.8)
template<typename Iterator, typename Func>
void async_for_each(Iterator first, Iterator last, Func f);

// parallel_find.hpp         (Listing 8.9)
template<typename Iterator, typename MatchType>
Iterator parallel_find(Iterator first, Iterator last, MatchType match);   // last if not found

// async_find.hpp            (Listing 8.10; a helper like the book's parallel_find_impl is up to you)
template<typename Iterator, typename MatchType>
Iterator async_find(Iterator first, Iterator last, MatchType match);      // last if not found

// parallel_partial_sum.hpp  (Listing 8.11)
template<typename Iterator>
void parallel_partial_sum(Iterator first, Iterator last);                 // in place

// barrier.hpp               (Listing 8.12, with 8.13's atomic count and done_waiting())
class barrier {
public:
    explicit barrier(unsigned count);
    void wait();
    void done_waiting();
};

// pairwise_partial_sum.hpp  (Listing 8.13; includes barrier.hpp)
template<typename Iterator>
void pairwise_partial_sum(Iterator first, Iterator last);                 // in place
```

Contract:
- **Up-front division (8.7, 8.9, 8.11), as in your 8.4 build:** at least 25 elements per thread, and `hardware_concurrency()` with a fallback of 2. The calling thread does the last block. A `join_threads` (your 8.4 header) does every join; there are no explicit join loops.
- **`parallel_for_each`:** each spawned block runs as a `std::packaged_task<void()>` wrapping a lambda over its range. Reading every future brings any worker's exception back to the caller.
- **`async_for_each` / `async_find`:** recursive halves with `std::async`. Split only when the range holds at least 2 × 25 elements. Random-access iterators (the book uses `first + length/2`). Exceptions come back through `get()`.
- **`parallel_find`:**
  - A `std::promise<Iterator>` plus a `std::atomic<bool>` done flag, checked before every element.
  - A match, or a caught exception, sets the promise and then the flag.
  - Setting a promise that's already set throws. Catch and discard that.
  - Every thread is joined **before** the flag is checked. If no match was found, return `last`; otherwise get the result (or the stored exception) through the future.
  - Any match is acceptable, not necessarily the first in the range.
- **`async_find`:**
  - The done flag is shared with every recursive call by reference (`std::ref` through `std::async`).
  - A match sets it; so does an exception, which is then rethrown.
  - A half that stops early returns its own `last`.
- **`parallel_partial_sum` (forward propagation):**
  - Each block is passed an iterator to its **last** element, the future for the previous block's last value, and the promise for its own.
  - It computes its own partial sum. If it has a previous value, it adds it to its last element **first**, passes that on, then updates the rest.
  - An exception goes into the block's promise, so it travels down the chain to the calling thread's block, which rethrows it.
- **`barrier`:**
  - `count`, `spaces` and `generation` are `std::atomic<unsigned>`, with `seq_cst` throughout.
  - `wait()` spins with `std::this_thread::yield()` until the generation changes. The last thread in resets `spaces` from `count` and bumps the generation.
  - `done_waiting()` removes the caller from future rounds **and** frees its space in the current round.
- **`pairwise_partial_sum`:**
  - One thread per element (`length - 1` spawned, plus the caller), plus a second buffer.
  - At each step, a thread reads from one of the two (the range or the buffer) and writes to the other, alternating between steps, with a `barrier::wait()` after each step. When its stride passes the start of the range, it calls `done_waiting()`.
  - **The result must be right for every length.**
- **Non-template functions defined in a header must be `inline`.** `barrier`'s members defined inside the class already are.
- **Heads-up 1, Listing 8.11 as printed doesn't compile:**
  - `return last;` inside a `void` function;
  - `value_type& addend = previous_end_value->get();` binds a non-const reference to the temporary that `get()` returns.
- **Heads-up 2, Listing 8.13 as printed gives wrong sums from length 3 up.** I checked in scratch: for 1, 2, 3, element 2 comes out as 5 instead of 6. Finding why is part of the build. The pointer: at each step, ask which slot a thread reads its addend from, and whether the thread that owns that slot has always written its *current* value there, especially threads that have already finished. My fixed copy passed every length from 2 to 200, with TSAN and ASan clean in 3 of 3 runs.

Tests to write (one driver, one block per listing; `std::vector` everywhere):
1. **`parallel_for_each` and `async_for_each` (8.7, 8.8).**
   - Fill 10,000 ints with 0..9,999 and apply `f = [](int& x){ ++x; }`. Assert element i is i + 1.
   - Then use an `f` that throws a `poison_error` on the element equal to 5. Assert the caller catches it.

   Do both checks for each function. *Catches:* a block skipped, done twice, or the last block lost; an exception escaping a worker (`std::terminate`).
2. **`parallel_find` and `async_find` (8.9, 8.10).** On 10,000 distinct ints 0..9,999:
   - finding 7,000 returns an iterator at position 7,000;
   - finding -5 returns `last`.

   For `parallel_find` only, also search for an absent value in a range of a driver type whose `operator==` throws `poison_error` on one element (put it at index 100), and assert the caller catches it. *Catches:* a block never searched; waiting for ever on the promise when the value is absent; a lost exception.
3. **`parallel_partial_sum` (8.11).** For lengths 1, 10 (one thread), 10,000 and 10,001, fill `long long`s with `i % 7 + 1`. Assert the result equals `std::partial_sum` on a copy. *Catches:* a missing or doubled forward add, and off-by-one errors at block edges.
4. **`barrier` (8.12/8.13).**
   - **Lockstep:** 4 threads × 1,000 rounds. In round r, each thread writes r to its own slot, calls `wait()`, checks all 4 slots equal r (counting mismatches in an atomic), then calls `wait()` again. Assert 0 mismatches. In scratch: 0 mismatches in 3 of 3 TSAN runs.
   - **Dropout:** `barrier(4)`. One thread calls `wait()` 10 times, then `done_waiting()`. Three threads call `wait()` 100 times. Assert all four finish. A `done_waiting()` that doesn't free its space hangs here; in scratch, `verify.sh`'s timeout caught that.

   *Catches:* a thread running ahead of the others, and a dropout that strands the rest.
5. **`pairwise_partial_sum` (8.13).** For lengths 1, 2, 3, 100 and 127, fill `long long`s with `i % 7 + 1`, and assert the result equals `std::partial_sum` on a copy. Stay at or under about 200 elements, since it's one thread per element. *Catches:* the printed listing's bug (it fails from length 3), and stale reads across the two arrays.

Self-explanation (2–4 sentences, closed-book, before review):
- Why must `parallel_find` join every thread before it checks the flag, rather than just blocking on the future?
- Why does the book say Listing 8.11 can't be rewritten with `std::async`?
- Why must `done_waiting()` decrement `spaces` as well as `count`?

Answer:


---

## Review: 8.5 (2026-11-02)

This was a build-only worksheet, so there are no warm-ups or quiz to grade. Testing was kept short, at your request: one `verify.sh` batch, and one scratch driver run 3 times.

### Warm-up

None. This was a build-only sheet (your decision); the due items stay due.

### Quiz

None. This was a build-only sheet.

### Build

```
parallel_algorithms: TSAN 0/4 clean (0 race reports) | ASan+UBSan 0/4 clean | 2-TU link OK
```

That was `-n 4 -t 60`, 13 s. **Every run dies in the driver at line 66** (the `for_each` assert, explained below) before any library code is checked.

**Scratch: library check.** I took a copy of your driver and made three changes:
- the two `for_each` asserts check every element;
- the find-exception test searches for an absent value;
- the listed barrier tests are added.

Result: TSAN 3/3 clean with 0 warnings, ASan+UBSan 3/3 clean, and every assert passes.

**Library: what's right.**
- **Listing 8.13: you found and fixed the book's bug.** `else buffer[i] = ith_element;` before `done_waiting()` puts every finished element's value in both arrays. All five listed lengths are correct. ✔
- Listing 8.11's two compile errors are avoided. `const value_type& addend = ...get()` extends the temporary's lifetime, so it's fine.
- The barrier, both `for_each` versions and the success paths of both `find` versions are correct.

**Library: four problems** (all in F20):
- **(a) Exceptions are sliced.** `async_find` and `parallel_partial_sum` do `catch (const std::exception& e) { ...; throw e; }`. That throws a *copy* of the `std::exception` part, so the caller can no longer catch the real type. In scratch, `async_find` on a range with a poisoned element reached the caller as a plain `std::exception`, not a `poison_error`. The book uses `catch(...)` and `throw;`, which rethrows the original. `catch (const std::exception&)` also misses non-`std` exceptions. In `parallel_find` that's a worker exception escaping its thread, so `std::terminate`.
- **(b) `parallel_find`'s handler can throw.** If another thread has already set the promise, `set_exception()` throws `std::future_error` out of the catch block on a worker thread, which calls `std::terminate`. The book wraps it in a second `try { ... } catch(...) {}`. Your driver's current find test can trigger exactly this race: a match in the caller's block and the poison in a worker's block.
- **(c) `parallel_partial_sum` makes blocks one element too long.** It uses `std::advance(block_last, block_size)` where it should be `block_size - 1`, so `block_last` is one past the block. Each spawned block gets `block_size + 1` elements. On this 2-core box that's still correct, but with enough threads the blocks run off the end. In scratch, with `hardware_concurrency()` replaced by 32, 800 elements gave an ASan `heap-buffer-overflow`.
- **(d) Nothing is in `namespace par`,** against the contract. `barrier`, `parallel_for_each` and the rest are global names.

**Driver, against the Tests-to-write list.**
- **Test 1 (`for_each`): the asserts can't do their job.** Each lambda returns inside its first loop iteration. At `i == 0` it evaluates `test_values[i - 1]`, and `i - 1` wraps to the largest `size_t`, which crashes the run (SEGV). It never looks at element 1 or later. The two exception checks are fine. ✗
- **Test 2 (`find`): the exception check searches for `throwing_type(5000)`, which is present.** It's the first element of the calling thread's block, so the caller matches at once, the flag stops the worker before index 100, and no exception happens. That fails the assert in scratch. The spec said to search for an absent value. The four success asserts are fine. ✗
- Test 3 (`parallel_partial_sum`): written. ✔
- **Test 4 (barrier lockstep and dropout): missing.** ✗
- Test 5 (`pairwise_partial_sum`): written, and it catches the printed listing's bug. ✔
- **Self-explanation: blank.**

Nit (one line): `int length = std::distance(...)` narrows.

### Queue updates

No warm-ups to log and no quiz misses. 8.5 closes, so here are its core items:

```
python3 tools/review.py add 8.5 8.5-find-join-before-flag "parallel_find (8.9) joins every thread before checking done_flag: if nothing matched, no one sets the promise and get() would block for ever; the flag only means something once every searcher has finished" --kind core --source "designing-concurrent-code-in-practice-notes.md 8.5 build (parallel_algorithms; Listing 8.9)"
python3 tools/review.py add 8.5 8.5-partial-sum-forward-chain "Listing 8.11: each block waits on the previous block's last value (future), updates its own last element first and passes it on (promise), then updates the rest; exceptions travel down the promise chain to the caller's block. Tasks wait on each other mid-run, so it can't be written with std::async" --kind core --source "designing-concurrent-code-in-practice-notes.md 8.5 build (parallel_algorithms; Listing 8.11)"
python3 tools/review.py add 8.5 8.5-barrier-generation-dropout "Barrier: read generation before taking a seat; the last arrival refills spaces from count, then bumps generation; waiters spin on generation, not spaces; done_waiting() must decrement spaces as well as count, or the current round never completes" --kind core --source "designing-concurrent-code-in-practice-notes.md 8.5 build (parallel_algorithms; Listings 8.12-8.13)"
```

### Fix list

- **Opened F20** (`parallel_algorithms`):
  - library:
    - (a) `catch(...)` and `throw;` in place of `catch (const std::exception& e)` and `throw e;`;
    - (b) guard `parallel_find`'s `set_exception()`;
    - (c) `block_size - 1` in `parallel_partial_sum`;
    - (d) `namespace par`;
  - driver:
    - (e) the `for_each` asserts;
    - (f) an absent value in the find-exception test;
    - (g) the barrier tests;
  - (h) the self-explanation.

  Re-verify: `verify.sh parallel_algorithms -t 60` (default 20).

### Beyond the book (ungraded)

None.
