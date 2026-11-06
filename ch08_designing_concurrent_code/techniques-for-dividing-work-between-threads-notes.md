# 8.1 — Techniques for dividing work between threads

## 8.1 intro, 8.1.1 (dividing data up front), 8.1.2 (dividing data recursively, Listing 8.1), 8.1.3 (dividing work by task type)

```
$ python3 tools/review.py due   (2026-10-26)
Due now: 103 active (101 overdue) | maintenance due: 1 | due in next 3 days: 5 | total active: 108, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.1-data-race-definition  (sec 5.1, 2d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.1 Q3 gave 2 of 3 conditions (missed 'at least one not atomic'))
  2. [miss] 5.2-rmw-result-not-reload  (sec 5.2, 28d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.4–5.2.5 Q2
  3. [miss] 5.1-modification-order  (sec 5.1, 27d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.1 Q5 missed the non-atomic half; Q7 headline said 'yes' (correct: no))
Also due (next worksheets): 100 more (5.2-atomic-free-functions, 5.2-atomic-udt-reasons, 5.2-atomics-not-copyable, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 17 (F1-F17)
Lint: 86 items more than 7 days overdue (oldest 3.2-deadlock-both-locks-held, 31d)
```

**Plan (2026-10-26, your decision):** chapter 7 is paused after 7.2.4, and 8.1 is `current`. The first run ends after chapter 9. The backlog (review-only worksheets) comes after that, so this worksheet doesn't raise it.

Triage:
- **8.1 intro (generalist vs specialist threads):** scene-setting; its content is 8.1.3's. Not quizzed.  
- **8.1.1 + 8.1.2 facts:** the reduction step and how to parallelize it; why Quicksort can't be divided up front; `std::async` vs a thread per recursion vs Listing 8.1; the costs the book names. **Recall** (Q1).
- **8.1.3:** the wrong-concerns symptoms and remedies, and the pipeline-vs-data-division timing. **Recall** (Q2).
- **Listing 8.1 is a reusable mechanism, so it's a build:** `parallel_quick_sort` (below; added at your request). Why a waiting thread sorts chunks instead of just waiting, how the worker count stays bounded, and which members worker threads touch are covered by the build and its self-explanation, so they aren't quizzed. (The first version of this worksheet had a code-tracing question that showed the whole listing; it's gone, since it would have given the build away.)
- **Not graded:** *why* contention on the stack hurts even when it's lock-free (the book defers it to 8.2.1), and thread-pool problems (chapter 9).

**One low/med/high tag per answer**, warm-ups included.

### Warm-up

1. The position thread and the report thread share three things:

```cpp
double pnl = 0;                  // plain
std::atomic<int> fills{0};
bool halted = false;             // plain; every access holds mutex m

// position thread                                   // report thread
pnl += trade_pnl;                                    print(pnl);
fills.fetch_add(1, std::memory_order_relaxed);       print(fills.load(std::memory_order_relaxed));
{ std::lock_guard<std::mutex> g(m); halted = true; } { std::lock_guard<std::mutex> g(m); print(halted); }
```

   (a) Give chapter 5's definition of a data race, with every condition.
   (b) For each of the three pairs: race or not? Name the condition that decides it. For `fills`, the accesses are relaxed and nothing orders them: which condition keeps that pair legal?
   (c) If a pair is a race, what does the program have?

Answer:

(a) A data race occurs when:
- two threads access the same memory location concurrently.
- there is no enforced ordering between their operations.
- at least one of the operations is a write operation.
- at least one of the operations is non-atomic.

(b) 
- for `pnl` we have a race because there is no surrounding atomics in either thread to produce any ordering among the two operations. the position thread operation is a write, the variable itself is non atomic and so essentially all of the conditions for a race condition are fulfilled.
- for `fills` even with relaxed ordering, what we wont have is a race condition since both operations will come one after the other, i.e., atomic operation guarantees no overlap among individual operations on the atomic variable, hence the name atomic, i.e., atomicity in the operations happening on this variable among different threads.
- for `halted` we also dont have a race condition because we have a mutex guarding every access to this plain variable which means that none of the accesses are actually concurrent which means the first condition of a race condition of concurrent access never comes to pass.

(c) if a pair is a race condition the program has UB or undefined behavior, which means the language standard makes zero guarantees about the entire program's behavior.

2. The order router hands out client order IDs:

```cpp
std::atomic<long> next_id{1000};
long id = (next_id += 1);        // old code
```

   To choose a memory order, someone changes the second line to call `next_id.fetch_add(1, std::memory_order_relaxed)`.
   (a) What value did `+=` give `id`? What does `fetch_add` return?
   (b) Write the one line that gives `id` the same value as the old code.
   (c) A colleague writes `next_id.fetch_add(1, std::memory_order_relaxed); long id = next_id.load(std::memory_order_relaxed);`, and 6 strategy threads call it. What can go wrong?

Answer:

(a) in old code, `+=` gives the new value to the `id` thats also the current value of next_id. `fetch_add` returns the old value of next_id to the id variable.

(b) 
```cpp
long id;
long expected;
do{
   id = next_id.load() + 1; 
   expected = id - 1; 
}
while (!next_id.compare_exchange_strong(expected, id));
```

(c) the code is:

```cpp
next_id.fetch_add(1, std::memory_order_relaxed); 
long id = next_id.load(std::memory_order_relaxed);
```
since the orderings are relaxed as opposed to acquire-release or sequentially consistent, this means that the fetch_add by one thread need not register across the other thread's caches which may lead to wrong decisions in their operations downstream as they read and use `id`. Furthermore there is the obvious race window in between the fetch_add and load which might be a problem if the code was to get the single increment read inside the same particular thread, i.e., if next_id value was 10 and thread A ran fetch_add to make it 11 and then loaded it in the id in the following line, it might get 12 or 14 because the other six threads may have come between the race window mentioned between the fetch_add and load.


3. `bid_seq` and `ask_seq` are `std::atomic<int>`, both 0. Thread W1 does `bid_seq.store(1, relaxed)`; thread W2 does `ask_seq.store(1, relaxed)`. Readers R1 and R2 each read both variables over and over, with relaxed loads.
   (a) What is an object's modification order? For a plain `int`, who is responsible for every thread agreeing on it, and what do you have if they don't? For an atomic, who is responsible?
   (b) R1 sees `bid_seq` become 1 while `ask_seq` is still 0. Can R2 see `ask_seq` become 1 while `bid_seq` is still 0? Must the readers agree which variable changed first? Can R1 later read `bid_seq == 0` again?

Answer:

(a) an object's modification order is the history of its mutations, i.e., what value the variable has held across its lifespan. With the appropriate memory order tags, the C++ standard provides the guarantees associated with the orderings, for example with the default sequentially consistent ordering, the standard guarantees that all thread have to agree with all operations happening on atomic variables and on a more granular level this global order of operations translates to enhanced cache coherency traffic between the cores. If such ordering is not enforced where needed, we are open to different types of race conditions.

(b) Can R2 see `ask_seq` become 1 while `bid_seq` is still 0? YES - we have relaxed memory order tags, only atomicity and modification order are respected. Must the readers agree which variable changed first? NO - in the above context at least, they dont need to, for stronger memory orderings they do, but for the current relaxed orderings they dont. Can R1 later read `bid_seq == 0` again? NO - modification order is respected no matter the memory order tag, since R1 has read bid_seq as 1, it cant read a previous value in its modification order.


### Quiz

1. (Recall) 8.1.1 and 8.1.2.
   (a) In the divide-up-front pattern, what is the final step called? What is it for a parallel `accumulate`, and for a parallel `for_each`? How does Listing 2.8 do that step, and what are the book's two ways to do better?
   (b) Why can't Quicksort's data be divided up front? Why are its two recursive calls safe to run concurrently?
   (c) Chapter 4's parallel Quicksort ran the lower chunk with `std::async`. What decision does that hand to the library? Give the book's two problems with starting a new thread at every level of recursion instead.
   (d) Which two costs does the book say Listing 8.1's stack-of-chunks design brings, and what does it say the design is a specialized version of?

Answer:

(a) in the divide up front pattern of work division, the final step is called a reduction where we make the final merge of all the partial results returned from the different threads. listing 2.8 does a serial accumulation for the last reduction step with a call to std::accumulate. the book states two ways of doing the final reduction step, one is the serial reduction where one thread does the merging like in listing 2.8 and the other is parallel tree reduction where parallelize the reduction step by summing the partial sums in different threads in a recursive fashion.

(b) quick sort algorithm has the following steps:
- choose a pivot point, often the first element
- make a pass and rearrange the entire list such that the left side is less than and the right side is greater than or equal to the pivot, giving us the two halves or partitions.
- the recursively call the sorting function on the two halves separately, thereby allowing this partitioning to ultimately reach base case of a single value and on the unwind we start to combine the smaller partitions until we end up with the full sorted array on the caller stack.

the step two is what makes upfront data division impossible because unless we run the partition, we can not know the size of both partitions, its an intermediary step of the algorithm that makes the data separation. the thread has to already be started to discover the two partitions.

the two recursive calls are safe to run concurrently because the algorithm runs independently on the two halves all the way until it reaches the single element base case.

(c) chapter 4's version of quick sort starts a recursion on one half of each recursive call and it does so using std::async which starts a new thread each time its called. this is problematic because such a setup leads to exponential increase in thread count. Even a modestly large data set can spawn thousands of threads and since thread creation and management has significant overhead, it can lead to an overall slowdown of the system and the function's operation.

(d) the stack-of-chunks design is a specialized version of the work stealing thread pool implementation which is upcoming in chapter 9. I dont recall any specific discussion of cost of this approach in the book, though if i had to argue a cost, it would be the additional complexity added to the code for a task as simple as a sort on a list.

2. (Recall) 8.1.3.
   (a) What two symptoms does the book say show you separated the wrong concerns, and what do both boil down to? Give the book's two remedies, and when each applies.
   (b) 12 items, 3 cores, and each item needs 3 steps of 2 seconds each. Dividing the data (4 items per thread): how many items are finished at 6 s and at 12 s, and when is the batch done? With a 3-stage pipeline: when is the first item finished, how often after that, and when is the batch done? How long until the last core starts on its first item?
   (c) Apart from the timing profile, in what two situations does the book say a pipeline is the right fit? Why does the video player prefer it, even though the total time is longer?

Answer: 

(a) here are the two symptoms:
- first one is - if there are two threads sharing / reading-writing the same data far too much, then they are not truly separate concerns and thus should be joined together in one thread or the shared concern in them be pulled out and made into a new thread.
- second one is - if there are two threads that often endup waiting for each other, or more particularly, thread A often ends up waiting for thread B, then that is also indicative that they were meant to be one thread because of tight sequential coupling which implies that they arent separate concerns.

(b) If upfront data division approach is taken and its assumed that each thread does the entire algorithm end to end, then to clear one item, each thread needs 6 seconds, and that means that in the first 6 seconds, batch of 3 items will be done and in 12 seconds, batch of 6 items will be done.

If on the other hand, a sequential pipeline approach is taken, then, the priming will take the first 6 seconds. for the stage 3 thread to touch the last (12-th item) my diagram estimates that to happen at 28 second mark. for the first item to finish, the pipeline will take 6 seconds and it will at that point be primed and will produce one finished item every two seconds unlike the data division approach producing a batch of 3 items every 6 seconds. after 4 seconds or at the 5th second, the last, stage-3 core will touch the first item during the priming stage.

(c) the book recommends using the pipeline division in realtime systems where the pipeline has to handle an incoming stream of data such as video, audio, sensor, or orderbook update data, since here smooth low latency output matters. On the other hand, data division approach is recommended for when the raw throughput matters and smoothness doesn't matter. In video streaming for example, even if the data division approach might allow for more frames per second on average over a longer timeframe, if every second the screen freezes only to give 60 frames per second for the next second before freezing again, this is unwatchable, but if we have the sequence based processing implemented, we might be getting 30 fps on any particular instance but since there are no screen freezes on the middle for the next batch of frames to be prepared, thats acceptable for any viewer.


### Build task — `parallel_quick_sort` (Listing 8.1)

Folder: `ch08_designing_concurrent_code/parallel_quick_sort/` (scaffolded: `CMakeLists.txt`, the root `add_subdirectory` line, and empty header and driver). Book closed.

Interface:

```cpp
// hardware_threads - 1, leaving room for the thread that calls do_sort.
// When hardware_threads is 0 (hardware_concurrency() couldn't tell): 1,
// matching parallel_accumulate's fallback of 2 threads in total.
unsigned default_max_workers(unsigned hardware_threads);

template<typename T>
class sorter {
public:
    sorter();                               // cap = default_max_workers(std::thread::hardware_concurrency())
    explicit sorter(unsigned max_workers);  // cap given directly (for tests); 0 is allowed
    ~sorter();                              // stops every worker thread, then joins it
    sorter(const sorter&) = delete;
    sorter& operator=(const sorter&) = delete;

    std::list<T> do_sort(std::list<T>& chunk_data);   // returns the sorted elements; chunk_data is consumed
    std::size_t worker_count() const;       // worker threads started so far; only call it while no do_sort is running
};

template<typename T>
std::list<T> parallel_quick_sort(std::list<T> input);   // one sorter per call, as in the book
```

Thread-safety contract:
- **Layout, as in the book:**
  - a `chunk_to_sort` holds a `std::list<T>` and a `std::promise<std::list<T>>`;
  - a stack of pending chunks, a `std::vector<std::thread>` of workers, and a `std::atomic<bool> end_of_data`.

  No `std::async` and no thread per chunk.
- **The chunk stack:** its `pop()` returns a null `std::shared_ptr` when the stack is empty, and nothing in it copies a chunk (a `std::promise` can't be copied).
  - Your 3.2.3 `threadsafe_stack` doesn't fit as it is: its `pop()` throws when the stack is empty, and copies the element. Don't edit that target. Write a small lock-based stack in this header instead.
  - The ch7 stacks don't fit either, since their `push()` copies.
- **`do_sort()`:**
  - It splits around the first element, as the book does.
  - It pushes the lower chunk and keeps that chunk's future.
  - It starts a new worker only while the number started is below the cap.
  - It sorts the higher part itself (recursively), then gets the lower result from the future.
- **A thread waiting for its lower chunk must never block on a chunk that no thread will ever take off the stack.** A sort must finish with `max_workers == 0`.
- **Worker threads run `do_sort()` too.** Every member `do_sort()` touches must be safe while several threads run it at once. `verify.sh`'s TSAN runs are the check.
- **Workers:** each loops (try one chunk, then `std::this_thread::yield()`) until `end_of_data` is set. The destructor sets it and joins every worker, so no thread outlives the sorter.
- **Never more than the cap of worker threads,** including when `hardware_concurrency()` returns 0.
- **Exceptions:** assume `T`'s comparison and moves don't throw.
- **Non-template functions defined in the header must be `inline`.** The 2-TU link check fails otherwise (F3/F5/F16).

Tests to write:
1. **Sorts correctly while workers race.** Build a `sorter<int>(4)` and sort 20,000 random ints drawn from [0, 1000), so there are many duplicates. Assert the result equals a copy sorted with `std::list::sort()`, and that `worker_count() <= 4`. Do it 3 times, with a fresh sorter each time. *Catches:* lost or duplicated elements, a result set on the wrong promise, and (through TSAN) unsynchronized access to shared members.
2. **No workers doesn't hang.** Build a `sorter<int>(0)` and sort 2,000 random ints from [0, 1000). Assert the result is sorted and that `worker_count() == 0`. *Catches:* a waiting thread that blocks on a chunk nobody will pop. That shows up as a `verify.sh` timeout, not a failed assert.
3. **The thread count stays bounded.**
   - Assert `default_max_workers(0) == 1`, `default_max_workers(1) == 0` and `default_max_workers(8) == 7`.
   - Sort 20,000 random ints from [0, 1000) with a default-constructed `sorter<int>`. Assert the result is sorted and that `worker_count() <= default_max_workers(std::thread::hardware_concurrency())`.
   - Assert `parallel_quick_sort` returns an empty list for an empty input, and the same single element for a one-element input.

   *Catches:* the unsigned `0 - 1` wrap-around, and a new worker for every chunk.

**Keep the inputs as listed: random, from [0, 1000), at most 20,000 elements.** Both limits are about the call stack, and neither is a bug to fix:
- An already-sorted list makes this first-element Quicksort recurse once per element.
- A thread that runs a chunk while it waits does it inside its own `do_sort()` call, so calls nest much deeper than the Quicksort recursion alone.

I checked a correct version of Listing 8.1 in scratch. With distinct values, nesting reached about 2,900 calls, and a 20,000-element sort with 1 worker overflowed the stack under ASan in 6 of 12 runs (100,000 elements overflowed too). With values from [0, 1000), nesting stayed under about 810 calls, and every run passed under ASan and TSAN, with 1, 2 or 4 workers.

Self-explanation (2–4 sentences, closed-book, before review): Why does a thread that is waiting for its lower chunk sort chunks off the stack, instead of just calling `get()`? What goes wrong without that, both with zero workers and with every worker waiting? Which members of `sorter` do worker threads touch, and what makes each one safe?

Answer:


### Beyond the book (optional, ungraded)

The book's pipeline example has equal step times. With unequal ones, a pipeline's steady rate is set by its **slowest** stage, and each item's latency is still the sum of the stage times plus a queue hand-off per stage, so a pipeline raises throughput but never lowers per-tick latency. That's why low-latency trading pipelines usually keep the hot path on one thread, or use single-producer/single-consumer ring buffers with each stage pinned to its own core. Not graded; queued only if you say "queue it".

---

## Review: 8.1 intro, 8.1.1–8.1.3 (2026-10-29)

**All three warm-ups were answered**, the first time since 7.2.1. There were **no confidence tags** anywhere, and the **self-explanation is blank**.

### Warm-up

- **`5.1-data-race-definition` → miss.**
  - (a) is right: all four conditions, including "at least one of the operations is non-atomic".
  - (b) is wrong on the pair being probed. For `fills`:
    > "atomic operation guarantees no overlap among individual operations on the atomic variable"

    What keeps that pair legal is the condition you listed in (a): both accesses are atomic, so "at least one non-atomic" fails. Relaxed operations don't order or serialize anything. The pair stays unordered, and still isn't a race.
  - For `halted`, you wrote "none of the accesses are actually concurrent". The book's wording: the mutex enforces an ordering between the two accesses, so the "no enforced ordering" condition fails.
  - The `pnl` pair and (c), undefined behaviour, are right.
- **`5.2-rmw-result-not-reload` → miss.**
  - (a) is right.
  - (b) asked for one line. The answer is a `load() + 1` / `compare_exchange_strong` loop. It works, but it ignores the return value that (a) just named, and it uses the default `seq_cst`, which drops the relaxed order the change was made for. The line is `long id = next_id.fetch_add(1, std::memory_order_relaxed) + 1;`.
  - (c): the window ("it might get 12 or 14") is right. But "the fetch_add by one thread need not register across the other thread's caches" is the visibility model again. A memory order doesn't change *when* a write is seen. The window is the whole problem.
- **`5.1-modification-order` → miss.**
  - (b) is right on all three points: the readers needn't agree across two variables, and R1 can't go back to 0.
  - (a) doesn't say who is responsible:
    - for a plain `int`, you are, through synchronization. If threads disagree, that's a data race, so undefined behaviour.
    - for an atomic, the compiler and library guarantee it.

    The answer talks about `seq_cst` and cache-coherency traffic instead. It's the same half missed in the original 5.1 Q5. The definition itself is fine: the order of every write to that object, starting from its initialization.

### Quiz

**Q1 → miss.**
- (a): "reduction" and Listing 2.8's serial `std::accumulate` are right.
  > "the other is parallel tree reduction where parallelize the reduction step by summing the partial sums in different threads in a recursive fashion"

  The recursive call is one of the book's two ways. The other is to have the worker threads do some of the reduction steps as each one finishes, instead of starting new threads. "Serial reduction" isn't a way to do better; it's what Listing 2.8 already does. Also missing: for `for_each` the final step is a no-op, because there's nothing to reduce.
- (b) is right: partitioning is what reveals the halves, and the two calls touch separate sets of elements.
- (c):
  > "std::async which starts a new thread each time its called"

  That's wrong. `std::async` lets the Thread Library decide when to run the task on a new thread and when to run it synchronously; that's the decision it hands over. The book's two problems with a thread per recursion are that too many threads slow the application down, and that a large data set can run out of threads. You gave the first.
- (d):
  > "a specialized version of the work stealing thread pool"

  The book says "a specialized version of a thread pool". Work stealing is a chapter 9 topic.
  > "I dont recall any specific discussion of cost of this approach"

  The book gives two costs. Your guess (the complexity of managing and communicating between the threads) is one. The other: every thread pushes to and pops from the same stack, so there's heavy contention on it, even with a lock-free stack.

**Q2 → miss.**
- (a): both symptoms are right ("sharing ... the same data far too much", and threads that "end up waiting for each other"). The book's summary, that both boil down to too much communication, is missing. Both remedies are there, but you attached both to the first symptom. The book's conditions:
  - if all the communication is about the same issue, make that issue one thread's job and take it out of the others;
  - if two threads communicate a lot with each other and little with the rest, combine them.
- (b) is right: 3 items at 6 s and 6 at 12 s; for the pipeline, the first item at 6 s, then one every 2 s; and the last core starts after 4 s. Missing: when the data-division batch is done (24 s). The "28 second mark" is when the 12th item *finishes*; stage 3 starts it at 26 s.
- (c):
  > "the book recommends using the pipeline division in realtime systems where the pipeline has to handle an incoming stream of data"

  That's one of the two situations: the input isn't all known when the operation starts (data coming over a network, or a filesystem scan). The other is missing: each operation in the sequence is time consuming. The video reason is right; the book adds that viewers accept a couple of seconds' delay at the start.

  **Correction:** you wrote "smooth low latency output". A pipeline doesn't lower latency in the book's example. The first item still takes the full 12 s, and the whole batch takes longer. What it gives you is regular output.

### Build

```
parallel_quick_sort: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link FAILED: ODR multiple definition of: default_max_workers(unsigned int)
```

The full run took 73 s, with `-t 60`.

**Library.** You fixed the book's race on `threads`:
- the check-and-start is under a mutex;
- `workers` is sized up front, with an atomic count;
- the destructor joins only the workers that were started.

The help-while-waiting loop is there, and the stack fits (null on empty, holds `shared_ptr`s, push by move). Two bugs:
- **(a) `default_max_workers` isn't `inline`,** so the 2-TU link fails. This is the fourth time (F3, F5, F16), and the spec had a contract bullet for it.
- **(b) `explicit sorter(unsigned max_workers)` initializes `max_thread_count(default_max_workers(max_workers))`.** The contract says the cap is the argument itself, and 0 is allowed. In scratch:
  - `sorter(0)` started 1 worker;
  - `sorter(1)` started 0;
  - `sorter(4)` started 3.

  The sorts were still correct.

**Scratch mutants (copies, not your files):**
- **No help-while-waiting loop:** your driver hung in 3 of 3 runs, before Test 1 finished. The loop matters, and the driver catches its absence.
- **No lock around the worker start:** TSAN reported nothing in 5 of 5 runs.
  - With your up-front `workers` and an atomic count, the unlocked version only goes wrong when two threads read the same count at once. Both then write the same slot: the cap is exceeded, and the second move-assign onto a joinable `std::thread` calls `std::terminate`.
  - Your lock is right.
  - **Spec slip (mine):** Test 1's "catches unsynchronized access (through TSAN)" was checked only against the book's `push_back` version, not against this design.

**Driver, against the Tests-to-write list.**
- Test 1: written; the second loop is the listed test. Its asserts can fail. ✔
- **Test 2 is not as listed.** It builds `sorter(1)` and has no `worker_count() == 0` assert. `sorter(1)` runs with zero workers only because of bug (b). The listed `sorter(0)` with that assert would have caught (b).
- Test 3: written. All of its asserts can fail. ✔
- **Self-explanation: blank.**

Notes (not fix items):
- Print the `random_device` seed, so a failing run can be reproduced.
- `uniform_int_distribution<int>(0, 1000)` includes 1000.
- The header's `threadsafe_stack` and `empty_stack` are in the global namespace, with the same names as the 3.2.3 ones. A file that includes both headers gets a redefinition error, and two files that include one each give an ODR clash. A namespace fixes it; 9.1 may include both.
- `do_sort_running` is written but never read.

### Queue updates

```
python3 tools/review.py log 5.1-data-race-definition miss --note "8.1 warm-up: listed all four conditions incl. 'at least one non-atomic', but justified the relaxed fills pair by 'atomic guarantees no overlap', not by that condition; halted pair put down to 'not concurrent' rather than mutex-enforced ordering"
python3 tools/review.py log 5.2-rmw-result-not-reload miss --note "8.1 warm-up: (a) right; (b) wrote a seq_cst load+CAS loop instead of fetch_add(1, relaxed) + 1; (c) window right, but said relaxed fetch_add 'need not register across other threads' caches' (visibility model)"
python3 tools/review.py log 5.1-modification-order miss --note "8.1 warm-up: (b) all three right; (a) didn't say who is responsible (you, via sync, for non-atomic, else data race; the compiler/library for atomics); talked about seq_cst and cache coherency instead"
python3 tools/review.py add 8.1 8.1-divide-data-facts "Reduction step (for_each: a no-op); the book's two ways to parallelize it (accumulate calls itself recursively; workers do reduction steps as each finishes); std::async lets the library choose new thread vs synchronous; a thread per recursion: too many threads slow it down, and you can run out; Listing 8.1's costs: complexity, and contention on the one shared stack; it is a specialized thread pool" --kind miss --source "techniques-for-dividing-work-between-threads-notes.md 8.1 Q1"
python3 tools/review.py add 8.1 8.1-pipeline-fit "Wrong concerns = too much communication; remedies: all of it about one issue -> make it one thread's job; two threads chatting mostly to each other -> combine them. A pipeline fits when the input isn't all known at the start, or each step is time consuming. It makes output regular; it does not lower first-item latency, and total time gets longer" --kind miss --source "techniques-for-dividing-work-between-threads-notes.md 8.1 Q2"
python3 tools/review.py add 8.1 8.1-help-while-waiting "Listing 8.1: a thread waiting for its lower chunk sorts chunks off the stack; a plain get() hangs with zero workers, or when every thread is waiting on chunks still on the stack" --kind core --source "techniques-for-dividing-work-between-threads-notes.md 8.1 build (parallel_quick_sort)"
python3 tools/review.py add 8.1 8.1-hardware-concurrency-zero-cap "hardware_concurrency() may return 0; unsigned hardware_concurrency()-1 then wraps to UINT_MAX, so Listing 8.1 starts a thread per chunk; fall back to a fixed count" --kind core --source "techniques-for-dividing-work-between-threads-notes.md 8.1 build (parallel_quick_sort)"
python3 tools/review.py add 8.1 8.1-workers-run-do-sort "Worker threads run do_sort too, so Listing 8.1's threads.size() check and push_back race (a data race on the vector); the check-and-start needs a lock (or start every worker up front)" --kind core --source "techniques-for-dividing-work-between-threads-notes.md 8.1 build (parallel_quick_sort)"
```

### Fix list

- **Opened F18** (`parallel_quick_sort`):
  - (a) make `default_max_workers` `inline` (the 2-TU link check);
  - (b) `sorter(unsigned max_workers)` must use `max_workers` as the cap, not `default_max_workers(max_workers)`;
  - (c) Test 2 as listed: `sorter(0)` and `assert(worker_count() == 0)`;
  - (d) the self-explanation.

  Re-verify: `verify.sh parallel_quick_sort -t 60` (default 20 runs).

### Beyond the book (ungraded)

None. The worksheet's pipeline note wasn't queued.
