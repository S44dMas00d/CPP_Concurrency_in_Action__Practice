# 8.4 — Additional considerations when designing for concurrency

## 8.4 intro, 8.4.1 (exception safety, Listings 8.2–8.5), 8.4.2 (Amdahl's law), 8.4.3 (hiding latency), 8.4.4 (responsiveness, Listing 8.6)

```
$ python3 tools/review.py due   (2026-10-31)
Due now: 113 active (107 overdue) | maintenance due: 1 | due in next 3 days: 0 | total active: 113, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 4.1-notify-outside-lock  (sec 4.1, 4d overdue, streak 0)
     source: waiting-for-an-event-or-condition-notes.md 4.1 Q7 (Correction 2026-10-05; Q7 was mis-graded at the original review)
  2. [misconception] 5.2-cas-weak-vs-strong  (sec 5.2, 4d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.3 Q2
  3. [misconception] 5.3-sync-needs-value-read  (sec 5.3, 4d overdue, streak 0)
     source: synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(b)(c)
Also due (next worksheets): 110 more (5.1-data-race-definition, 5.2-atomic-free-functions, 5.2-atomic-udt-reasons, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 18 (F1-F18)
Lint: 94 items more than 7 days overdue (oldest 3.2-deadlock-both-locks-held, 36d)
```

**8.2 and 8.3 are `skipped`** (your call: descriptive advice, better reread on a later pass). You've read them, so later graded questions may use them, but nothing here relies on them.

Triage:
- **8.4 intro:** scalability defined; exception safety is a matter of correctness, scalability isn't. Folded into Q1.
- **Listings 8.2–8.4 (the throw points, `packaged_task` + futures, `join_threads`) are a reusable mechanism, so they're a build:** `safe_parallel_accumulate` (below). It's a bounded extension of your 2.4 `parallel_accumulate`, and 9.1's thread pool reuses `join_threads`. Which throw points call `std::terminate` and why, and how each fix removes one, are covered by the build and its self-explanation, so they aren't quizzed.
- **Listing 8.5 (`std::async`) and the "more than one thread threw" case:** **Recall** (Q1).
- **8.4.2 Amdahl's law:** the formula, the numbers, why it's naive, how to raise the gain, and the two kinds of scalability. **Recall** (Q2).
- **Listing 8.6 (GUI thread and task thread):** its event handlers misbehave on some orders of events. **Code-tracing** (Q4).
- **Not graded:** 8.4.3's mention of the lock-free queue (that's 7.2.6, which you haven't read), and the task switching it cites from 8.2.5.

**One low/med/high tag per answer**, warm-ups included.

### Warm-up

1. The risk thread trips the kill switch:

```cpp
// risk thread                                // order thread
{ std::lock_guard<std::mutex> g(m);           std::unique_lock<std::mutex> lk(m);
  kill_switch = true; }                       cv.wait(lk, []{ return kill_switch; });
cv.notify_one();                              cancel_all_orders();
```

   A reviewer says: "Between the risk thread's unlock and its `notify_one()`, the order thread can check `kill_switch`, go to sleep, and miss the notification for ever. Move the notify inside the braces."
   (a) Is the reviewer right? Walk through the two orders the order thread can reach `wait()` in (before the risk thread locks `m`, and after it unlocks), and say what `wait()` checks each time and while holding what.
   (b) What does chapter 4 say you gain by notifying *after* unlocking?

Answer:


2. Two CAS loops in a trading system:

```cpp
// (A) bump a counter
int cur = hits.load();
while (!hits.compare_exchange_???(cur, cur + 1)) {}

// (B) replace the fee table: building the new one copies and re-sorts 2,000 entries
fee_table* cur = table.load();
fee_table* fresh = build_from(*cur, update);          // expensive
while (!table.compare_exchange_???(cur, fresh)) {
    delete fresh; fresh = build_from(*cur, update);   // rebuild against the new cur
}
```

   (a) What is a spurious failure, and why is it possible on a machine with no single compare-and-exchange instruction?
   (b) What does `compare_exchange_strong` contain on such a machine? So what is `_strong` inside a loop like (A)?
   (c) Pick `_weak` or `_strong` for (A) and for (B), with chapter 5's reason for each. In (B), what exactly does a spurious failure cost you with `_weak`?

Answer:


3. The feed thread publishes a price:

```cpp
// feed thread
last_px = 101.5;                                   // plain double
ready.store(true, std::memory_order_release);

// strategy thread S1                              // strategy thread S2
double p = last_px;                                if (!ready.load(std::memory_order_acquire)) {
if (ready.load(std::memory_order_acquire))             double q = last_px;   // fall back to the old price
    use(p);                                        }
```

   (a) S1's load reads `true`. Is S1's read of `last_px` ordered after the feed thread's write? Name the relation that is or isn't there.
   (b) S2's load reads `false`. Is S2's read ordered with the write? Name the relation.
   (c) Where a relation is missing, what does the program have? Is "S2 just gets the old price" a correct description?

Answer:


### Quiz

1. (Recall) The 8.4 introduction and 8.4.1.
   (a) Why does the book deal with exception safety before scalability?
   (b) In Listing 8.5 (`std::async`, recursive halves), what happens when the direct recursive call throws, and when the asynchronous call throws? Which property of the future makes the first case safe?
   (c) Where does Listing 8.5 get its thread count from, and where do some of the "asynchronous" calls actually run?
   (d) In Listing 8.4, if two worker blocks throw, what reaches the caller? What does the book suggest if that matters?

Answer (med):

(a) The reason the book touches exception safety before scalability is that each thread we launch in a multithreaded program runs on its own call stack, if a function on its call stack throws an exception and it propagates to the last call on the stack and nothing catches the exception, then the C++ runtime calls std::terminate() which kills the entire process or program and not just the thread itself. this is in stark contrast to single threaded code where even if an exception is thrown and propagates all the way up to main(), it can still be caught in a try-catch there and thus avoid a complete crash. On a worker thread, propagate to caller is not possible because the caller is on its own call stack and this thread has a separate first frame on its own call stack. 

(b) consider the snippet:

```cpp
{
   std::future<int> f = std::async([](){return expensive_computation(); });
}
```
starting a thread using std::async() and getting a future is exception safe because if an exception is thrown and f goes out of scope before f.get() gets called, then f's destructor blocks until the async task completes, this property makes std::async trivially exception-safe compared to raw threads because we avoid thread leak and the async task is guaranteed to finish before f is destroyed.

now to discuss the listing, first case is that the second half recursive call, which happens on the same thread, throws an exception:
```cpp
T second_half_result = parallel_accumulate(mid_point, last, T());
```
the exception simply travels up the stack to the caller and since the first half caller was through std::async, the future that will now not get the chance to be called get() on and get added, it will go out of scope and its destructor will wait / block to complete the first half task and once it finishes cleanly, it will then allow the future destructor to return and we get no thread leak.

in the second scenario, we get an exception thrown in the std::async recursive call:
```cpp
std::future<T> first_half_result = std::async(parallel_accumulate<Iterator, T>, first, mid_point, init);
```
now the task throws the exception internally, the exception is stored in the future itself and the future will rethrow the exception when we call .get() on it here:
```cpp
return first_half_result.get() + second_half_result;
```
and this is where the exception will get passed to the caller frame. So, we didn't need a try-catch or error handling code to have exception safety.

(c) the parallel accumulate in Listing 8.5 implemented using std::async has an issue, it doesn't use std::thread::hardware_concurrency() to determine its thread count, it relies on the operating system for that, we as users of std::async() don't have control over its thread count, the runtime does and depending on availability of resources, it may run some async() calls synchronously rather than on separate threads when we call .get() on their futures. We can for sure say it will use N new thread instances for N async calls.

(d) if multiple worker threads throw exceptions, only one will be propagated to the caller, if it really matters, the book recommends using std::nested_exception to capture all the exceptions into one object and throw that.

2. (Recall) 8.4.2.
   (a) Write Amdahl's law, saying what f_s and N are.
   (b) A program's serial fraction is 0.2. Give the estimated speedup on 4 processors, on 16, and the limit as N grows without bound.
   (c) Give the book's two reasons this picture is naive.
   (d) Give the book's two ways to raise P. Then name the two aspects of scalability the book says you must choose between before picking a way to divide work.

Answer (med):

(a) Amdahl's law gives us an idea of an application's performance scalability with additional cores. its reliant on us dividing a program 1 into fractions of serial and parallel sections of code, here is the mathematical expression of this:

`f_s`: serial portion/ fraction of the code which has to run on one thread.
`1-f_s`: parallel portion/ fraction of the code which can run on N threads.

Amdahl's law then becomes:

`P = 1 / (f_s + (1-f_s)/N)` 

where N is the number of cores and f_s is the serial fraction.

So, if we have 16 cores and a f_s of 0, then we get a 16x speedup in theory, where as if the f_s is 1/3, or 1/3 of the code is serial in nature, so with 16 cores P comes out to be 2.67, so our relative speedup is dramatically reduced with a more realistic serial fraction. if we choose infinite number of cores, we get a theoretical speedup of 3 times. So the lesson is that even a small and realistic measure of serial code substantially reduces scalability of an application.

(b) with a 0.2 serial fraction, the speedup on different cores comes to be:
```
N = 4: P = 2.5
N = 16: P = 4
N = inf: P = 5 (speedup ceiling is 5 )
```

(c) the two reasons this picture is naive are:
- it assumes all cores are always busy on some task and never waiting
- it assumes that the problem or task, even if parallel is infinitely divisible, i.e., a so recursive quick sort which launches a new thread on each half will eventually reach a base case of single element half no matter if we threw a million cores at it.

(d) 


4. (Code-tracing) Listing 8.6, condensed. Every handler runs on the GUI thread.

```cpp
std::thread task_thread;
std::atomic<bool> task_cancelled(false);

void task() {
    while (!task_complete() && !task_cancelled) do_next_operation();   // each step: up to 2 s
    if (task_cancelled) perform_cleanup();
    else post_gui_event(task_complete);
}
void process(event_data const& event) {
    switch (event.type) {
    case start_task:    task_cancelled = false; task_thread = std::thread(task); break;
    case stop_task:     task_cancelled = true;  task_thread.join();              break;
    case task_complete: task_thread.join();     display_results();               break;
    }
}
```

   (a) The user clicks Start, then clicks Start again while the task is still running. What happens on the second click, and why?
   (b) The task finishes and posts `task_complete`. Before the GUI thread handles that event, the user clicks Stop. Trace both handlers in the order they run. What goes wrong, and where?
   (c) The user clicks Stop in the middle of a 2-second step. What does the user see until that step ends, and why? Which promise of 8.4.4 does that break?

Answer (med):

(a) 

(b)

(c)


### Build task — `safe_parallel_accumulate` (Listings 8.3–8.4)

Folder: `ch08_designing_concurrent_code/safe_parallel_accumulate/` (scaffolded: `CMakeLists.txt`, the root `add_subdirectory` line, and two empty headers and an empty driver). Book closed. A bounded extension of your 2.4 build, kept as its own target; leave `parallel_accumulate` as it is.

Interface:

```cpp
// include/join_threads.hpp
class join_threads {
public:
    explicit join_threads(std::vector<std::thread>& threads);
    ~join_threads();                      // joins every thread in the vector that is joinable()
    join_threads(const join_threads&) = delete;
    join_threads& operator=(const join_threads&) = delete;
};

// include/safe_parallel_accumulate.hpp
template<typename Iterator, typename T>
T safe_parallel_accumulate(Iterator first, Iterator last, T init);
```

Thread-safety contract:
- **Same division as Listing 2.8:** at least 25 elements per thread, `hardware_concurrency()` with a fallback of 2 when it returns 0, and the calling thread sums the last block itself.
- **Every spawned block runs as a `std::packaged_task<T(Iterator, Iterator)>`.** Its result, or its exception, comes back through the task's `std::future`. There is no results vector written through a reference.
- **An exception thrown anywhere reaches the caller of `safe_parallel_accumulate`.** That means a worker's block, the calling thread's own block, or a `T` operation while adding up the results. `std::terminate` is never called, and no thread is left unjoined when the function exits.
- **If several blocks throw,** exactly one exception reaches the caller (any one).
- **A `join_threads` object does all the joining.** It's constructed right after the threads vector. There's no explicit join loop and no `try`/`catch`.
- **`init` is added exactly once.** Each block starts from `T()`.
- Requirements on `T`: default-constructible and copyable. `Iterator`: at least a forward iterator.
- **Put any helper struct (the book's `accumulate_block`) in a namespace, or give it another name.** Your 2.4 header already defines `accumulate_block` in the global namespace, and 9.1 may include both (the F18 note about `threadsafe_stack` again).
- **Non-template functions defined in a header must be `inline`.** The 2-TU link check fails otherwise. `join_threads` member functions defined *inside* the class are already inline; ones defined outside it aren't.
- **Heads-up:** constructing the `packaged_task` from a temporary function object with parentheses is chapter 2's "most vexing parse". It declares a function, and GCC rejects the next line. The book's own listing has this problem.

Tests to write (driver-side type, your design: a `T` whose `+` with an element throws a `poison_error` when the element is -1, and `+=` for adding results):
1. **Correct sums.** Sum the ints 1..10,000 with `init = 7`. Assert the result equals `std::accumulate` with the same `init`. Also sum a 10-element range (one thread only) and an empty range (returns `init`). *Catches:* `init` added once per block, a dropped or double-counted block, and the last block lost.
2. **A worker's exception reaches the caller.** Use 10,000 elements with -1 at index 0 (always in the first spawned block). Assert the call throws `poison_error` and that the program carries on. Then run a clean sum, and assert it's right. *Catches:* an exception escaping a spawned thread (`std::terminate`), and a future that is never read.
3. **The calling thread's exception doesn't strand the workers.** Use 10,000 elements with -1 at the **last** index, which is in the calling thread's own block, while the workers are still joinable. Assert `poison_error` reaches the caller. Then put -1 at both index 0 and the last index, and assert exactly one `poison_error` is caught. *Catches:* missing `join_threads` (a joinable `std::thread` destroyed during unwinding calls `std::terminate`). I checked in scratch that a version without it aborts on this test every time.
4. **`join_threads` on its own.** Make a vector of 4 threads, each sleeping about 50 ms and then setting its own `std::atomic<bool>`. Add one default-constructed `std::thread` to the vector. Let a `join_threads` go out of scope. Assert all 4 flags are set and no thread in the vector is `joinable()`. *Catches:* a missed join, and calling `join()` on a thread that isn't joinable (that throws `std::system_error`).

Self-explanation (2–4 sentences, closed-book, before review): In Listing 8.2, which throw points end in `std::terminate`, and for which two different reasons? How does `packaged_task` + future remove one of them, and `join_threads` the other? Why can Listing 8.4 read the futures before the threads are joined, when Listing 8.2 had to join before reading `results`?

Answer: uncaught exceptions in worker threads lead to std::terminate. the thread constructor can throw and after the first worker thread was spawned if one of the later constructor calls for std::thread in the accumulate for loop throws, thats going to cause the successfully instantiated thread objects to be destroyed as the scope is left and without the joins on their underlying threads we have a std::terminate. another place is the last accumulate_block call which comes before worker thread joins, if that throws, we again are unable to join the worker threads before leaving the scope and thus get std::terminate. 

`packaged_task` + future allows the exceptions to be stored in the future itself and these can be returned gracefully to the caller at the end in the return statement allowing easier cleanup and `join_threads` allows that cleanup to happen automatically no matter where we leave the scope from.

because in 8.2, the results[] array positions were passed to threads directly and the values cannot be accessed reliably before threads are joined and their accumulations are confirmed as done, on the flip side in 8.4 with futures, calling .get() on the future will lead that call to block until that thread completes the calculation and we for sure have the value in the future and that value is then returned to the caller variable so this makes sense in 8.4. we do still need to join those threads though if we don't use the join_threads class as we see in 8.3.


### Beyond the book (optional, ungraded)

8.4.1 says a future from `std::async` waits in its destructor. Futures from `packaged_task` and `promise` don't (the book doesn't say so). That's why Listing 8.4 still needs `join_threads` even though it uses futures. Separately, `std::async` with the default policy may run a task lazily, on the thread that calls `get()`. That's how "some calls run synchronously in `get()`" happens. Not graded; queued only if you say "queue it".

---

## Review: 8.4 intro, 8.4.1–8.4.4 (2026-11-01)

The three warm-ups, Q2(d) and Q4 are blank. **The self-explanation is written, and it's right**: the first one since F11. The quiz answers are tagged (all med).

### Warm-up

- `4.1-notify-outside-lock` → **miss** (blank).
- `5.2-cas-weak-vs-strong` → **miss** (blank).
- `5.3-sync-needs-value-read` → **miss** (blank).

### Quiz

**Q1 → miss.**
- (a):
  > "if a function on its call stack throws an exception and it propagates ... then the C++ runtime calls std::terminate()"

  That's 8.4.1's point: why exceptions are worse in parallel code. It isn't the intro's reason for the order. The intro's reason: exception safety is a matter of **correctness** (broken invariants, race conditions, or termination), while code that doesn't scale still works.
- (b) is right: a throw from the recursive call destroys the future, and its destructor waits for the task; an exception from the asynchronous task is stored in the future and rethrown by `get()`. The property: the destructor waits.
- (c) is half right, then contradicts itself.
  > "it may run some async() calls synchronously rather than on separate threads when we call .get()"

  That part is the book's point.
  > "We can for sure say it will use N new thread instances for N async calls."

  This contradicts the line before. The book: the library makes the calls use the hardware threads available *without creating an overwhelming number of threads*, so some calls never get a thread of their own. It's the same `std::async` misconception as 8.1 Q1(c).
- (d) is right: one exception propagates; use `std::nested_exception` if it matters.

**Q2 → miss.** (a)–(c) are right:
- the formula, with f_s and N;
- the speedups for f_s = 0.2: 2.5, 4 and 5;
- both reasons it's naive: tasks aren't infinitely divisible, and not everything is CPU-bound, because threads wait.

(d) is blank. The book's answer:
- **Two ways to raise P:** shrink the serial sections or the time threads spend waiting; or give the system more data, so the parallel sections stay primed with work.
- **The two aspects of scalability:** reducing the time one action takes, or processing more data in a given time. Decide which matters to you before choosing how to divide work.

**Q4 → miss** (blank). The intended answers:
- (a) The second Start move-assigns a new `std::thread` onto `task_thread` while it's still joinable, so `std::terminate` is called (chapter 2's rule, the same as the destructor case).
- (b) Stop sets the flag and joins (the task has already finished). Then the `task_complete` handler calls `join()` on a thread that is no longer joinable, which throws `std::system_error`.
- (c) Stop's `join()` blocks the GUI thread until the current step ends, up to 2 s. The window freezes, which breaks 8.4.4's promise that the GUI thread "is always able to respond to the events in a timely fashion".

### Build

```
safe_parallel_accumulate: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK
```

The 4-run batch took 7 s.

**Library: correct.**
- Blocks run as `packaged_task`s, and results and exceptions come back through the futures.
- `join_threads` is declared after both vectors, so it's destroyed first, and it joins before the futures and threads go.
- The vexing parse is avoided with braces. ✔
- **One contract miss:** `accumulate_block` is in the global namespace. In scratch, a file including your 2.4 `parallel_accumulate.hpp` and this header fails to compile: `error: redefinition of 'struct accumulate_block<Iterator, T>'`.

**Scratch mutant:** `join_threads` removed (with an explicit join before the results are read). Your driver aborted 3 of 3 times, on Test 3. ✔

**Driver, against the Tests-to-write list.**
- Test 1: written; all three asserts can fail. ✔
- Test 2: written, and its assert can fail. The listed "then run a clean sum" after the throw is missing. It's not a fix item: the function keeps no state between calls, so that step adds almost nothing.
- Test 3: written; it's the test that catches the mutant. ✔
- Test 4: written; both asserts can fail. ✔

**Self-explanation: written, and correct.**
- Both `std::terminate` paths: an exception escaping a worker's function, and joinable threads destroyed when a later `std::thread` constructor or the caller's own block throws.
- Which fix removes which.
- Why `get()` makes the early join unnecessary.

Nits (one line, not fix items): the interface asked for an `explicit` constructor and deleted copy operations on `join_threads`; `int length` narrows `std::distance`; `<stack>` and `<mutex>` are unused.

### Queue updates

```
python3 tools/review.py log 4.1-notify-outside-lock miss --note "8.4 warm-up: left blank"
python3 tools/review.py log 5.2-cas-weak-vs-strong miss --note "8.4 warm-up: left blank"
python3 tools/review.py log 5.3-sync-needs-value-read miss --note "8.4 warm-up: left blank"
python3 tools/review.py add 8.4 8.4-correctness-and-async-threads "Exception safety is correctness (broken invariants, races, terminate) while code that doesn't scale still works; Listing 8.5 gets its thread use from the library, which avoids an overwhelming number of threads, and some async calls run synchronously in get(), so NOT one new thread per call" --kind miss --source "additional-considerations-when-designing-for-concurrency-notes.md 8.4 Q1"
python3 tools/review.py add 8.4 8.4-amdahl-raise-p "Raise P: shrink the serial sections or the time threads wait, or give more data so the parallel sections stay primed; the two aspects of scalability: less time per action vs more data per unit time; pick which matters before dividing work" --kind miss --source "additional-considerations-when-designing-for-concurrency-notes.md 8.4 Q2(d)"
python3 tools/review.py add 8.4 8.4-gui-task-handler-misuse "Listing 8.6: Start while a task runs move-assigns onto a joinable std::thread -> std::terminate; task_complete handled after Stop already joined -> join() on a non-joinable thread throws std::system_error; Stop's join() blocks the GUI thread until the current step ends" --kind miss --source "additional-considerations-when-designing-for-concurrency-notes.md 8.4 Q4"
python3 tools/review.py add 8.4 8.4-two-terminate-paths "Listing 8.2 dies two ways: an exception escaping a spawned thread's function, and a joinable std::thread destroyed during unwinding (a later thread constructor or the caller's own block throws); packaged_task + future fixes the first, join_threads the second" --kind core --source "additional-considerations-when-designing-for-concurrency-notes.md 8.4 build (safe_parallel_accumulate; self-explanation)"
python3 tools/review.py add 8.4 8.4-get-replaces-early-join "Listing 8.4 reads the futures before joining because get() blocks until that task is done; Listing 8.2's results vector needed the join first" --kind core --source "additional-considerations-when-designing-for-concurrency-notes.md 8.4 build (safe_parallel_accumulate)"
python3 tools/review.py add 8.4 8.4-async-future-dtor-waits "Listing 8.5 is exception safe because destroying a std::async future without waiting makes its destructor wait for the task (no dangling thread); the async task's exception comes back through get()" --kind core --source "additional-considerations-when-designing-for-concurrency-notes.md 8.4 Q1(b)"
```

### Fix list

- **Opened F19** (`safe_parallel_accumulate`): put `accumulate_block` in a namespace, or rename it, so the header can be included next to 2.4's `parallel_accumulate.hpp`. Re-verify: `verify.sh safe_parallel_accumulate`, plus a one-file compile that includes both headers.

### Beyond the book (ungraded)

None. The worksheet's note wasn't queued.
