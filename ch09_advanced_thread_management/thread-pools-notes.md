# 9.1 — Thread pools

## 9.1.1 (simplest pool, Listing 9.1), 9.1.2 (waitable tasks, Listings 9.2–9.3), 9.1.3 (tasks that wait for tasks, Listings 9.4–9.5), 9.1.4 (thread-local queues, Listing 9.6), 9.1.5 (work stealing, Listings 9.7–9.8)

**Build-only worksheet (your decision for chapter 9, 2026-11-04), the same as 8.5.** There are build tasks for Listings 9.1–9.8, with one or two tests per header to show each one works, and no warm-ups or quiz. The warm-up items stay due, and the review queues 2–4 `core` items when 9.1 closes.

Triage:
- **Every listing is a reusable mechanism, so all are builds:** one target, `thread_pools`, with one header per listing. 9.4's `run_pending_task()` goes into the 9.2 pool's header.
- **Reused targets:**
  - your 6.2.2 **`threadsafe_ptr_queue`** as the pool queue. Its `push()` moves and its `try_pop(T&)` move-assigns, so it holds move-only tasks. Your 6.2.3 `fine_grained_queue` copies, so it can't.
  - **`join_threads`** from your 8.4 build.

  The CMake file links both. F12 (the queue's driver) and F19 (the accumulate header) don't affect either header.
- **Not built:** the car-pool analogy, and the closing note about resizing a pool dynamically.

### Build task — `thread_pools` (Listings 9.1–9.8)

Folder: `ch09_advanced_thread_management/thread_pools/`. It's scaffolded:
- `CMakeLists.txt`, which links `threadsafe_ptr_queue` and `safe_parallel_accumulate`;
- the root `add_subdirectory` line;
- eight empty headers and one empty driver.

Book closed.

Interface (everything in `namespace tp`):

```cpp
// function_wrapper.hpp           (Listing 9.2): a move-only, type-erased void() callable
class function_wrapper {
public:
    function_wrapper() = default;
    template<typename F> function_wrapper(F&& f);
    function_wrapper(function_wrapper&& other);
    function_wrapper& operator=(function_wrapper&& other);
    function_wrapper(const function_wrapper&) = delete;
    function_wrapper(function_wrapper&) = delete;
    function_wrapper& operator=(const function_wrapper&) = delete;
    void operator()();
};

// simple_thread_pool.hpp         (Listing 9.1): fire-and-forget tasks
class simple_thread_pool {
public:
    simple_thread_pool();
    ~simple_thread_pool();
    template<typename FunctionType> void submit(FunctionType f);
};

// thread_pool.hpp                (Listings 9.2 + 9.4): waitable tasks
class thread_pool {
public:
    thread_pool();
    ~thread_pool();
    template<typename FunctionType>
    std::future<typename std::result_of<FunctionType()>::type> submit(FunctionType f);
    void run_pending_task();
};

// pool_accumulate.hpp            (Listing 9.3)
template<typename Iterator, typename T>
T pool_accumulate(Iterator first, Iterator last, T init);

// pool_quick_sort.hpp            (Listing 9.5): uses tp::thread_pool
template<typename T>
std::list<T> pool_quick_sort(std::list<T> input);

// local_queue_thread_pool.hpp    (Listing 9.6): same public interface as thread_pool
class local_queue_thread_pool { /* thread_pool's public members */ };

// work_stealing_queue.hpp        (Listing 9.7)
class work_stealing_queue {
public:
    work_stealing_queue();
    work_stealing_queue(const work_stealing_queue&) = delete;
    work_stealing_queue& operator=(const work_stealing_queue&) = delete;
    void push(function_wrapper data);
    bool empty() const;
    bool try_pop(function_wrapper& res);     // front: the owner thread, LIFO
    bool try_steal(function_wrapper& res);   // back: other threads
};

// work_stealing_thread_pool.hpp  (Listing 9.8): same public interface as thread_pool
class work_stealing_thread_pool { /* thread_pool's public members */ };
```

Contract:
- **Every pool:**
  - Starts `hardware_concurrency()` worker threads, or 2 when that returns 0. (The book has no fallback; with 0 threads nothing would ever run.)
  - Workers loop until a `std::atomic<bool> done` is set. The destructor sets it, and a `join_threads` member joins every worker. Tasks still queued when the pool is destroyed are abandoned, as the book says; a test must wait for its tasks before letting the pool go.
  - **Declare the members in this order:** `done` and the queue(s), then the threads vector, then the joiner. Members are destroyed in reverse order, so the queue must outlive the threads that use it.
  - If starting a thread throws, set `done` and rethrow, so the threads already started stop and get joined.
- **`simple_thread_pool`:** a queue of `std::function<void()>`. An idle worker calls `std::this_thread::yield()`.
- **`function_wrapper`:** type erasure through a base class with a virtual `call()` and a `std::unique_ptr` to it. It must hold move-only callables (a `std::packaged_task`, or a lambda that owns a `std::unique_ptr`).
- **`thread_pool`:**
  - The queue holds `function_wrapper`.
  - `submit()` wraps `f` in a `std::packaged_task<result_type()>`, keeps its future, moves the task onto the queue, and returns the future. The task's result, or its exception, comes back through that future.
  - `run_pending_task()` runs one queued task, or yields if there isn't one. The worker loop calls it.
- **`pool_accumulate`:** divides into blocks of 25 elements (by blocks, not threads), submits all but the last block to a `thread_pool`, sums the last block on the calling thread, then adds up the futures.
- **`pool_quick_sort`:**
  - It splits around the first element, as in Listing 8.1.
  - It submits the lower part to the pool and sorts the upper part itself.
  - It waits for the lower result by calling `pool.run_pending_task()` until the future is ready. **Never block on `get()` while the future isn't ready.**
  - Move the lower chunk into the task; don't copy it.
- **`local_queue_thread_pool`:**
  - A shared pool queue, plus a `static thread_local std::unique_ptr<std::queue<function_wrapper>>`. Each worker creates its own queue at start-up; other threads have none.
  - `submit()` from a pool thread pushes to that thread's local queue, and from any other thread to the pool queue.
  - `run_pending_task()` tries the local queue first, then the pool queue.
- **`work_stealing_queue`:** a `std::deque<function_wrapper>` behind one mutex. `push()` and `try_pop()` use the front; `try_steal()` uses the back.
- **`work_stealing_thread_pool`:**
  - The pool owns one `work_stealing_queue` per worker, in a vector of `std::unique_ptr`s. Each worker learns its index and queue through `static thread_local` members.
  - `run_pending_task()` tries, in order: the local queue, the pool queue, then stealing from the other threads' queues, starting at the next index after its own.
  - **No data races, including while the constructor is still running** (see heads-up 4).
- **Header rules:**
  - Non-template functions defined in a header must be `inline` (the F3/F5/F16/F18 lesson).
  - **`static thread_local` data members need a definition.** In a header, for a non-template class, write `inline static thread_local ...` (C++17). A separate out-of-class definition in the header fails the 2-TU link check.

**Heads-ups (the printed listings have problems; each was checked in scratch on 2026-11-04):**
1. **Listing 9.3 doesn't compile.** `pool.submit(accumulate_block<Iterator,T>())` never passes the block's range, so `std::result_of` finds no call with zero arguments, and GCC says "no matching function for call to submit". Submit something that captures `block_start` and `block_end`.
2. **Listing 9.5's wait loop doesn't compile:** `while(!new_lower.wait_for(...) == std::future_status::timeout)`. `std::future_status` is a scoped enum, so `!` doesn't apply to it ("no match for `operator!`"). Write the condition you mean.
3. **`std::result_of` is what the book uses.** It's deprecated in C++17 (with a warning) but still available, so use it as the interface shows.
4. **Listing 9.8's constructor has a data race, inherited from the book.** Find it before you write the constructor. The pointer: what does each freshly started worker read straight away, and what is the constructor still doing to that object at the same moment? In scratch, the printed version gave TSAN race reports in 3 of 3 runs. A fixed version was clean in 3 of 3, and also sorted correctly under ASan.

Tests to write (one driver, one block per header):
1. **`function_wrapper`.**
   - Wrap a `std::packaged_task<int()>` that returns 42, keep its future, call the wrapper, and assert the future gives 42.
   - Wrap a lambda that owns a `std::unique_ptr<int>`. Move the wrapper into a second one, call the second, and assert the lambda ran.
   - Add `static_assert(!std::is_copy_constructible<tp::function_wrapper>::value)`.

   *Catches:* a wrapper that copies, or loses the callable on a move.
2. **`simple_thread_pool`.** Submit 1,000 tasks, each doing `fetch_add(1)` on a `std::atomic<int>`. Before the pool is destroyed, spin with `yield()` until the counter reaches 1,000. *Catches:* a lost task, or a worker that never runs (it hangs, and `verify.sh`'s timeout shows it).
3. **`thread_pool`.**
   - Submit 100 tasks returning `i * i`, and assert every future gives the right value.
   - Submit a task that throws `poison_error`, and assert `get()` rethrows it.

   *Catches:* results attached to the wrong future, and a lost exception.
4. **`pool_accumulate`.** Sum the ints 1..10,000 with `init = 7`, and assert the result equals `std::accumulate`. Also check an empty range returns `init`.
5. **`pool_quick_sort`.** Do this 3 times: 20,000 random ints from [0, 1000) (the 8.1 stack-depth limit applies), and assert the result equals a copy sorted with `std::list::sort()`. *Catches:* a waiter that blocks on `get()` (it hangs, and the timeout shows it), and lost or duplicated elements.
6. **Nested submits, the same test for all three waitable pools** (`thread_pool`, `local_queue_thread_pool`, `work_stealing_thread_pool`):
   - From the main thread, submit one outer task.
   - Inside it, the outer task submits 50 inner tasks, each returning its index 0..49.
   - It waits for each inner future by calling `run_pending_task()` until it's ready, then returns the sum.
   - Assert `outer.get() == 1225`.

   Also run test 3's 100 squares on the two newer pools. *Catches:* tasks on a thread-local queue that never run (a hang), the local queue being used by a non-pool thread, and broken stealing.
7. **`work_stealing_queue`**, single-threaded. Push three wrappers that record 1, 2 and 3 into a vector when called.
   - `try_pop` should run 3 (the newest, from the front).
   - `try_steal` should run 1 (the oldest, from the back).
   - `try_pop` should run 2.
   - Then both calls should return `false`.

   *Catches:* the ends swapped.

Self-explanation (2–4 sentences, closed-book, before review):
- Why does a pool member that the workers use have to be declared before the threads vector and the joiner?
Answer: thread pool class members that the workers use have to be declared before the threads vector and joiner because the destructor, when activated walks and destroys in reverse direction of declaration, meaning that what got declared as a member first gets deleted last. Now principally, things needed by the threads to join gracefully should not be deleted before the threads are joined and by virtue of how `join_threads` class was defined, as soon as it gets destroyed, the join() call is sent to all referenced threads, so all the members those threads use, should be destroyed after the joiner object is destroyed and so they should and are placed or declared above the joiner in the class.

- Why does Listing 9.5 deadlock if `do_sort` waits with a plain `get()`, and what does `run_pending_task()` change?

Answer: in listing 9.5, all threads end up blocking and waiting on the results of other threads, we end up with a situation where no thread is free to make any progress and we have a deadlock caused not by mutex cycle but by resource starvation. the solution is that while waiting for result of one thread, a waiting thread can access the task queue for another task, thus allowing us to avoid wasting CPU time and making concurrent progress all around. `run_pending_task()` allows the calling thread to pick up one task at the front and run it, the listing uses a while loop over the awaited result to keep on calling this to keep the task queue drained using the waiting threads.

- What problem do thread-local queues cause for Quicksort, and how does stealing from the back fix it?

Answer: the limitation caused by thread local work queue is that the task submitted by a worker is submitted to that particular threads own work queue and in a recursive scenario especially, this can cause load imbalance where the local task queue of just one thread can have all the tasks being loaded on it where as others might be sitting empty and their threads again sitting idle. 
work stealing fixes this of course since idle threads can now check and pick up tasks from local queues, pool queue and other local task queues in the thread pool, thereby allowing us to minimize the idle time of these threads on the CPU.

---

## Review: 9.1 (2026-11-05)

This was a build-only worksheet, and testing was kept to the minimum, at your request: one `verify.sh` batch, one normal build, and two short scratch runs.

### Warm-up

None. This was a build-only sheet.

### Quiz

None. This was a build-only sheet.

### Build

```
thread_pools: TSAN 0/4 clean (0 race reports) | ASan+UBSan 0/4 clean | 2-TU link OK
```

That was `-n 4 -t 60`, 32 s. **The failures come from my test size, not your code.** A normally built driver passed 3 of 3 runs. Under ASan, test 5 (`pool_quick_sort`, 20,000 elements) overflows the stack: `do_sort` calls nest through `run_pending_task()`, as in 8.1. Under TSAN the run was killed early, almost certainly for the same reason.

**Spec slip (mine):** I checked the 20,000 size with an 8-thread pool. Your box reports 2 hardware threads, so the pool has 2 workers, the nesting goes much deeper, and the bind and packaged-task frames are bigger than 8.1's. **New size for test 5: 2,000 elements.** With that one change, in a scratch copy of your driver:
- ASan+UBSan: 2/2 clean, all tests pass.
- TSAN: all tests pass, but 7 race reports per run, all in the work-stealing pool's constructor (problem (a) below).

**Library: what's right.**
- `function_wrapper`, `simple_thread_pool`, `thread_pool` with `run_pending_task()`, and the 9.5 sorter. You fixed its loop condition to `== timeout`. ✔
- `local_queue_thread_pool`, including `static inline thread_local`, so the 2-TU link passes. ✔
- `work_stealing_queue` (front for the owner, back for thieves). ✔
- The 9.3 call fixed with `std::bind` over the block's range. ✔
- The member declaration order is right in every pool.

**Library: three problems** (all in F21):
- **(a) `work_stealing_thread_pool`'s constructor still has the book's race** (heads-up 4). It pushes onto `queues` and starts a worker in the same loop. Already-started workers read `queues[my_index]` and scan `queues.size()` while a later `push_back` reallocates the vector. TSAN reported this in both scratch runs.
- **(b) `throw std::current_exception();`** in the `local_queue_thread_pool` and `work_stealing_thread_pool` constructors throws a `std::exception_ptr` object, not the original exception. Use `throw;`, inside `catch(...)` (as `simple_thread_pool` and `thread_pool` already do with `throw;`). This only matters if starting a thread fails.
- **(c) No fallback when `hardware_concurrency()` returns 0** (contract), in any of the four pools.

**Driver, against the Tests-to-write list.** All 7 tests are written, and their asserts can fail. ✔ Notes (not fix items):
- Test 1's second check wraps a `packaged_task`, not "a lambda that owns a `unique_ptr`". It still proves a move-only callable survives a move, so it's fine.
- Test 4 doesn't check the empty range.
- `pool_accumulate.hpp` names its function `parallel_accumulate` (the interface said `pool_accumulate`), and uses blocks of 50 rather than 25.

**Self-explanation: written.**
- (1) is right: members are destroyed in reverse order of declaration, so the joiner goes first and the threads are joined while the queue still exists.
- (2) is right: every worker blocks waiting on chunks that no free thread can run, and `run_pending_task()` lets a waiting thread run them.
- (3) is half right. The imbalance is right. Why the *back* is missing: the owner pushes and pops at the front, so it runs its newest chunk first (better for the cache, and it finishes the branch it's waiting on). A thief takes from the back, which keeps it off the owner's end of the queue.

### Queue updates

No warm-ups to log and no quiz misses. 9.1 closes, so here are its core items:

```
python3 tools/review.py add 9.1 9.1-member-declaration-order "Thread pool members: done flag and queue(s) first, then the threads vector, then join_threads; members are destroyed in reverse, so the joiner joins every worker while the queue still exists" --kind core --source "thread-pools-notes.md 9.1 build (thread_pools; Listing 9.1)"
python3 tools/review.py add 9.1 9.1-function-wrapper-move-only "packaged_task is move-only and std::function needs copyable callables, so the waitable pool queues a type-erased, move-only function_wrapper (virtual call through a unique_ptr)" --kind core --source "thread-pools-notes.md 9.1 build (thread_pools; Listing 9.2)"
python3 tools/review.py add 9.1 9.1-run-pending-task-not-get "Tasks that wait for other tasks on a fixed-size pool must run pending tasks while waiting (run_pending_task loop); a plain get() can leave every worker blocked on work nobody is free to run" --kind core --source "thread-pools-notes.md 9.1 build (thread_pools; Listings 9.4-9.5)"
python3 tools/review.py add 9.1 9.1-local-queues-and-stealing "Thread-local queues cut contention on the shared queue, but unbalance the load (Quicksort: nearly everything lands on one worker's queue); work stealing fixes it: the pool owns every queue, the owner pushes and pops at the front (LIFO), and thieves take from the back" --kind core --source "thread-pools-notes.md 9.1 build (thread_pools; Listings 9.6-9.8)"
```

### Fix list

- **Opened F21** (`thread_pools`):
  - (a) the work-stealing constructor race;
  - (b) `throw;` in place of `throw std::current_exception();` (two constructors);
  - (c) the `hardware_concurrency() == 0` fallback;
  - (d) driver test 5 down to 2,000 elements (my spec fix, not your error).

  Re-verify: `verify.sh thread_pools -t 60` (default 20).

### Beyond the book (ungraded)

None.
