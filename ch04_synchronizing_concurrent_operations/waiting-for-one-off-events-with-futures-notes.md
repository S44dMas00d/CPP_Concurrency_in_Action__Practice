# 4.2 — Waiting for one-off events with futures

Session 16. Covers the section preamble (`std::future`/`std::shared_future` — the model, not yet the mechanics) and 4.2.1 (`std::async` — the first concrete way to actually get one). Genuinely reusable: `std::async` is the natural fit for "fire off N independent, slow operations concurrently and collect all the results" — exactly the shape of checking several exchanges' prices/order books at once instead of sequentially, so this gets a light build task alongside the quiz.

Warm-ups (2 — both 2.3 and 3.2 came due at session 16):

0a. (2.3, due since session 16 — fresh angle, not joinable-at-construction (3.2.1), not the self-move-assignment guard (3.2.4, miss), not the no-default-constructor question (3.2.8, hit).) Suppose someone suggests adding a `std::thread& get_thread()` accessor to `scoped_thread`, so external code could inspect (or even call `join()`/`detach()` directly on) the thread it's managing. What specific guarantee does `scoped_thread` currently provide that this would break?

Answer: In current scoped_thread implementation, while instantiating we supply a thread object to the constructor which gets moved into the internal thread object after checking that its joinable, and the destructor automatically runs join. the copy constructor and assignment operations are deleted where as move construction and assignment operators are operational. Unfortunately, reference returning or aliasing, which is what i think is happening in this function, i.e., a reference is being returned to the caller of `std::thread& get_thread()` - this is not going to be blocked because we deleted copy construction and assignment. Thus, in my estimation, there is no guarantee that post successful construction, there is any guarantee that some other alias wont be able to join or detach the internal thread object. there is a guarantee that this wont break the object since we check to ensure that the thread object is joinable before calling join in the destructor:

```cpp
    ~scoped_thread()
    {
        //  i know that t is private and thus cant be joined externally
        // and thus this check is superflous - however, i prefer having it
        // here nonetheless as it wont harm anything either.
        if (t.joinable()) {
            t.join();
        }
    }
```
However, I don't see any flip side guarantee in our implementation that it 'would' break. You could even go as far as saying that if we remove the guard clause in or destructor before the join - then we do have a guarantee of failure with external intervention through aliases, so if the interface has to guarantee this breakage then removal of this guard clause before join() is the step to make it so.

0b. (3.2, due since session 16 — session 14's `merge(donor, receiver)` warm-up was a miss, logged shaky. Fresh scenario, different domain, same underlying lesson — not a repeat of the Inventory question.) Two `Logger` objects, each with their own internal `std::mutex`, support `merge_logs(Logger& a, Logger& b)`, which interleave-merges every record from `b` into `a` — this genuinely requires holding *both* mutexes for the whole operation, not just one at a time, since producing a correctly time-ordered interleaving means reading from both simultaneously. Thread 1 calls `merge_logs(logX, logY)`; thread 2 concurrently calls `merge_logs(logY, logX)`. Walk through precisely how this deadlocks if both mutexes are locked one-at-a-time in fixed argument order, and state the fix.

Answer: so in the internal implementation, i will assume that we have the following individual mutex acquisition order:

```cpp
std::lock_guard<std::mutex> lk1(a.m);
std::lock_guard<std::mutex> lk2(b.m);
```
now suppose we have the call `merge_logs(logX, logY)` on thread 1 and then right there a microsecond later we have `merge_logs(logY, logX)` on thread 2, thread 1 has acquired mutex logX.m and thread 2 gets logY.m and then thread 1 tries for logY.m but blocks since thread 2 has it and thread 2 then tries for logX.m but blocks since thread 1 has lock over logX.m so we have a classic deadlock situation. 

The fix is simple, use the std::lock() free function to acquire locks on all supplied mutexes simultaneously, so now thread 1 and thread 2 function calls on merge_logs will become serialized. heres what that looks like:

```cpp
std::lock(a.m, b.m);
std::lock_guard<std::mutex> lk1(a.m, std::acquire_lock);
std::lock_guard<std::mutex> lk2(b.m, std::acquire_lock);
```

## Preamble — futures, the model

Recall:

1. `std::future<>` and `std::shared_future<>` are modeled after `std::unique_ptr`/`std::shared_ptr`. Spell out the analogy precisely: what does it mean for a `std::future` to be "the one and only instance referring to its associated event," and what changes once multiple `std::shared_future` instances refer to the *same* event — specifically, what happens to all of them at once, and what can each of them do?

Answer: `std::future<>` and `std::shared_future<>` are modeled after `std::unique_ptr`/`std::shared_ptr`. For std::future, this means that like unique_ptr, it cannot be copied, only moved, can only be moved from scope to scope and can only be moved across lvalues, furthermore, std::future is unique across all threads sharing the callable with that std::future, this means that unless you want a data race on the std::future while calling get on it from different threads in the callable, you protect it using mutex. the data race issue is where we like to introduce a more elegant fix that also allows for more concurrent operation of the callable thats shared among threads. the std::shared_future like std::shared_ptr can be copied across scopes and lvalues. with std::shared_future, each thread has its own copy of that shared_future object in its callable scope, that means that two threads can call get on it simultaneously and since the shared_future objects are copies, not unique, the get calls dont cause a data race. In addition to the race avoidance - all shared_futures point to the same underlying future event so all of the copies simultaneously become ready when the result is set or returned.

2. The book states future objects themselves don't provide synchronized access — if multiple threads need to touch a *single* future object, you're back to protecting it with a mutex, same as chapter 3. Given that, and given the text notes (without yet explaining the mechanism — that's 4.2.5) that multiple threads *can* each safely use their own `std::shared_future` copy without extra synchronization even though all the copies refer to the same result: what's the one structural detail already stated in this excerpt that's doing the work here — not the full mechanism, just what's actually been told to you so far?

Answer: the following details have been given for the shared future in the preamble:

std::shared_future<T>
- many receivers, copyable
- all copies become ready simultaneously
- each thread owns its own copy — safe to call .get() concurrently
- created from future via .share()

**Correction - (2026-10-05):**

The key structural detail is that `std::shared_future` is **copyable**, allowing each thread to have its **own `shared_future` object**, even though those copies refer to the same underlying result. This is different from multiple threads accessing the same `std::future` object. Therefore, each thread can safely call `.get()` on its own `shared_future` copy without needing to synchronize access to the `shared_future` objects themselves.

```cpp
std::promise<int> promise;
std::future<int> future = promise.get_future();

std::shared_future<int> shared = future.share();

std::shared_future<int> f1 = shared;
std::shared_future<int> f2 = shared;

std::thread t1([f1]() {
    int result = f1.get();
});

std::thread t2([f2]() {
    int result = f2.get();
});

promise.set_value(42);

t1.join();
t2.join();
```

Here, `f1` and `f2` are separate `shared_future` objects, so the threads aren't concurrently accessing the **same future object**, even though they share the same result.


3. A future's event, once it has happened, can't be reset. Back in 4.1 (already in your notes, Q5) you identified that a condition variable is a poor fit for an event a waiting thread will only ever wait on once, and that something else was better suited — now that you know what a future actually *is*, state precisely why "can't be reset" is exactly the property that makes it fit a genuinely one-off event, in a way a condition variable (reusable, no built-in one-shot guarantee) doesn't.

Answer: when using condition variable, we expect the code to be built around a repeating event, like a socket constantly receiving messages, a sensor frequently posting its data over a topic (PX4 - uORB reference), or a task queue often announcing another task being enqueued on the queue - so we have to structure the code around the condition_variable.notify() and condition_variable.wait() functions, i.e., these function have to be embedded in the producer and consumer functions respectively. For just one event, this setup looks unnecessary and ugly and not to mention, we have to block on the wait() so the wait thread must keep blocking further execution or consider instantiating another thread while breaking main task into more chunks because something will have to block until the event happens.

On the flip side, if we had only one throwaway task, or a long standing, unexpected wait time data call from say a large file parser and we would just tike to do our main job like data handling from a socket and as soon as the file data arrived, we change the way we execute our main task, then future is perfect, because we can then have a main while loop, or main script and we can do our usual task and use wait_for in the while case for the file data or use wait in the sequential script case at the very last possible position till we absolutely need the data from the file parser. the point is, we only need to place one object call in our code and we didn't have to engineer our whole paradigm with multiple threads, save one for the actual throwaway task, to get our task handled concurrently. Its convenient and clean.

## 4.2.1 — Returning values from background tasks

Recall:

4. What does `std::async` return, and what specifically does calling `.get()` on that return value do — both in the case where the result isn't ready yet, and in the case where it already is?

Answer: std::async, given the callable, returns std::future<T> where T is the return type of the callable given to the async. to make a future, a std::shared_future<T>, you'd have to call .share() on the future after its instantiated. calling .get() on a future blocks until the future is 'ready' which means that the async callable has run on its seperate thread and the result has been stored in the future, once this has happened, the get() returns the expected result of type T to the object lvalue that called .get() on the future. So, if the future isnt ready, the get call will block, otherwise it will do the aforementioned.

Code-tracing: 

5. Listing 4.7 shows `auto f1 = std::async(&X::foo, &x, 42, "hello");` (calls `p->foo(42,"hello")` where `p` is `&x`) right next to `auto f2 = std::async(&X::bar, x, "goodbye");` (calls `tmpx.bar("goodbye")` where `tmpx` is *a copy of* `x`). Both are calling a member function of `X` via `std::async` — what's the one-character-looking difference between these two call sites (`&x` vs. `x`) that actually changes *which object's state* ends up mutated by the call, if `foo`/`bar` mutate member data? Concretely: if `x.some_member` changes as a side effect, which of f1/f2's calls would you actually observe that change in `x` itself for, and why?

Answer: first, to directly answer the questions, for the first part, the address of character & is what ends up making the difference, i.e., in both calls member functions of the X class are being called, however, of what instance is determined by how we pass the object, if we pass &x, we are passing the actual object instance by reference, however, with just x, we are passing the instance by value and that leads to a copy in the async thread which the book specifies as tempx. So as a consequence the first call will endup showing side-effects in the actual instance we passed where as the second one won't, i.e., `auto f1 = std::async(&X::foo, &x, 42, "hello");` is the call where we will observer the side effects of the callable sent to async.

Recall / connect: 

6. Listing 4.7's `f5 = std::async(move_only());` works even though `move_only`'s copy constructor is explicitly deleted. What specifically makes this legal — name the rule, and which prior section you first learned it in (there are two candidates you've actually used this exact rule for already — either is fine, just be precise about which one you're citing).

Answer: yes the move_only() copy constructor and copy assignment constructor are deleted, however, its default constructor and move constructor arent deleted. in this snippet - `f5 = std::async(move_only());` first the default constructor will run to exectute just `move_only()` inside the call and it will produce an rvalue, and we know that for rvalues in callables as parameters, they get moved rather than copied, i.e., implicit move. So then we will have a call to the move constructor and we also have that available so that will run as the rvalue moves into the callable scope.

we learned these move semantics in 2.2 - passing arguments to a thread function and we also have discussed move semantics from time to time in chapter 3's subsections where it was relevant like how we can move unique_lock accross scopes, but the main anchor reference if i have to recall - is section 2.2 which also points towards move semantics in appendix A.

Gotcha (the centerpiece of 4.2.1):

7. `std::launch` has three relevant values: `std::launch::async` (run on its own thread, guaranteed), `std::launch::deferred` (don't run until `wait()`/`get()` is called on the future), and `std::launch::deferred | std::launch::async` (the *default* — implementation's choice). Two things to get precise: first, if you create `auto f7 = std::async(std::launch::deferred, baz, std::ref(x));` and your program never calls `f7.wait()` or `f7.get()` anywhere, does `baz` ever run? Second — the practically dangerous part — if you call plain `std::async(baz, std::ref(x))` with no launch policy specified, expecting it to run concurrently with the calling thread for a performance win, what's the actual guarantee you have that it will?

Answer: to answer the first part, if I create `auto f7 = std::async(std::launch::deferred, baz, std::ref(x));` and the program never calls `f7.wait()` or `f7.get()` anywhere, baz will not run because when we defer launch we defer it to when we block on get() or wait() and since we never do that, it wont run.

to answer the second part, if I call plain `std::async(baz, std::ref(x))` with no launch policy specified, it'll be like calling `std::async(std::launch::deferred | std::launch::async, baz, std::ref(x));`, i.e., the implementation will chose wether to run immediately or to defer to blocking function, this means there is no guarantee of having it run at will or immediately to give me the expected performance gain.

## Build task — `async_exchange_fetch`

Directly reusable, not just an exercise: firing off several slow, independent operations concurrently and collecting every result is exactly the shape of checking multiple exchanges at once instead of sequentially.

Rough shape (fill in the actual design yourself):

```cpp
// Stand-in for a slow network call to one exchange. Simulate the latency
// with a sleep_for of a consistent, meaningful duration (e.g. 100-200ms)
// so timing differences between launch policies are actually observable,
// not lost in noise.
double fetch_price(std::string const& exchange_name);

// Your job: fire off one std::async per exchange with an explicit launch
// policy, collect every result via get(), and return them mapped back to
// the exchange that produced them (order of completion isn't the same as
// order of the input list, once these genuinely run concurrently).
std::vector<double> fetch_all_prices(std::vector<std::string> const& exchanges);
```

What to actually prove (instrumented, not assumed):

1. **`std::launch::async` genuinely overlaps in time.** Time `fetch_all_prices()` across, say, 5 exchanges each simulating ~150ms of latency, using `std::launch::async` explicitly. Total wall-clock time should land close to ~150ms, not ~750ms — prove it with timestamps, the same style of proof you already built for `wait_and_pop()` in 4.1.
2. **`std::launch::deferred` does not** — same function, same simulated latency, but with `std::launch::deferred` instead. Since each deferred task only actually runs the moment its own future's `get()` is called, and you're calling `get()` on each in sequence, total time should land close to the *sum* (~750ms), not the concurrent figure from #1. This is the direct, empirical version of Q7's gotcha — reuses the same timing harness from #1, just swapping the launch policy, so this shouldn't cost you a second harness to build.
3. **Results map back to the right exchange**, even though completion order under real concurrency isn't guaranteed to match input order.
4. Somewhere in the driver (a small, separate check, not a big new harness): concretely demonstrate listing 4.7's f1-vs-f2 distinction from Q5 — call an async member function once via a pointer to a real object and once via a copy, and show the original object *is* mutated in one case and *isn't* in the other.

Self-explanation before review (2-4 sentences, closed-book): why does explicitly specifying `std::launch::async` matter here — what would you actually lose, silently, by leaving the launch policy at its default if you need these exchange checks to genuinely run in parallel?

Answer: as already explained and now proven in test as well, if we specify launch::async, async will immediately give a new thread to that callable if it has the cores so hardware concurrency utilization is confirmed, and this we see in test that five calls take 200 ms which is the time taken for 1 call to price function, and when we do launch::deferred, the launch is postponed to when we block at wait or get and we see this in test as well where we now get 1000 ms for the five threads which means we get full serialization since the get()s are called in a loop, so getting the flag wrong in a market-data case means a potentially unacceptable wait for results of throwaway tasks when they are eventually needed.

## Review

**Verdict: passable.** Both warm-ups, the whole quiz, and the build all in good shape overall — one real warm-up miss to correct, a couple of quiz precision notes, one real (but currently latent, non-manifesting) code defect worth fixing, and one small typo. Nothing here requires a redo.

### Warm-ups

0a (2.3, `get_thread()` accessor) — **miss.** The answer correctly recalls `scoped_thread`'s mechanics (move-only, joinable check at construction, guarded join in the destructor) but doesn't land the actual question. The real answer: exposing `get_thread()` would break `scoped_thread`'s core reason for existing — **exclusive ownership of the join responsibility**, the exact same invariant tested back at 3.2.1's very first warm-up (why `scoped_thread` throws on a non-joinable thread at construction) and stated explicitly in 2.1's own ledger note ("reference-based ownership leaves the door open to outside interference"). Concretely, two specific things would go wrong, not just an abstract "guarantee broken": (1) external code could `detach()` the thread through the leaked reference, silently making `scoped_thread`'s entire RAII promise — "when I go out of scope, the thread has been joined" — false, with nothing in `scoped_thread` itself able to detect this happened; (2) external code calling `join()`/`detach()` on the same `std::thread` object concurrently with `scoped_thread`'s own destructor doing the same is a genuine **data race on the `std::thread` object itself** — `std::thread`'s member functions aren't safe to call concurrently from multiple threads on the same object, so this isn't just a design smell, it's real UB. The `joinable()` guard in the destructor protects against calling `join()` on an already-non-joinable thread (avoiding `std::system_error`) — a real but much narrower concern than what the question was actually asking about. Logged as a miss — 2.3 resets to `shaky`, due again in 2.

0b (3.2, `Logger::merge_logs`) — **hit, correction needed on one name.** Part (a) is exactly right this time — both mutexes genuinely held simultaneously (not released-and-reacquired like the session-14 answer), correctly traced to the crossed circular wait. Part (b)'s concept (`std::lock()` + adopt-lock pattern) is exactly right too, but the actual standard tag is `std::adopt_lock`, not `std::acquire_lock` — you've used the correct name many times before (`account_transfer`, 3.2.4/3.2.6), so this reads like a slip rather than a gap. Banking this as 3.2's first clean hit — status moves from `shaky` back to `tested`, 1 hit banked, due in 4.

### Quiz

Q1 — correct on the core analogy (move-only vs. copyable, mirroring `unique_ptr`/`shared_ptr`) and correct on what happens once multiple `shared_future`s exist (all ready together, all can read). One phrase worth tightening: "`std::future` is unique across all threads sharing the callable" is a bit hand-wavy — the actual point is simpler: a `future` is just an ordinary non-thread-safe object like any other, so *if* multiple threads hold references to the *same* one, you're back to needing a mutex, same as any other shared mutable object from chapter 3 — nothing future-specific about the requirement itself.

Q2 — the facts you listed (each thread owns its own copy, copies become ready together, safe to call `.get()` concurrently) are all correct, but the question asked for the *reasoning*, not just the list: *why* does "each thread owns its own copy" solve the "future objects aren't synchronized" problem from the first half of the question? Because the synchronization concern was specifically about multiple threads touching the *same* object concurrently — once each thread has its own independent `shared_future` object (a copy), there's no shared mutable state *between the copies themselves* anymore, even though they all still read the same underlying async result. Worth having that connective "why," not just the bullet points, ready for an interview follow-up.

Q3, Q4, Q5, Q6, Q7 — all correct, several genuinely strong (Q3's real sensor/uORB connection, Q5's clean pointer-vs-copy trace). Nothing to add.

### Build — `async_exchange_fetch`

Ran your driver 10 times clean, timings landing exactly where they should: **~200ms with `std::launch::async`, ~1000ms with `std::launch::deferred`**, every run — a clean, real demonstration of Q7's gotcha, not just a described one. 10/10 clean under ASan+UBSan too. The results-mapping test is genuinely correct (not vacuous) — since `fetch_all_prices` stores each future's result by its original index regardless of completion order, the correctness check is real, not just "didn't crash." The pointer-vs-copy test (`x1`/`x2`) correctly demonstrates listing 4.7's lesson concretely.

Two things worth fixing:

1. **`fetch_price` and `fetch_all_prices` are defined directly in the header without `inline`.** This is currently harmless — only one `.cpp` file in this project includes this header — but it's a real ODR violation waiting to happen the moment a second translation unit includes it (multiple definitions of the same non-inline function across TUs), exactly the lesson from `thread_guard` (2.1) that's been correctly applied in every build task since — this is the first one where it slipped. Add `inline` to both. (Worth noting: my own scaffold for this header didn't repeat the usual ODR reminder comment this time, so this one's partly on me — still worth building the habit of applying it regardless of whether the reminder's there.)
2. **Minor, not blocking:** `assert(x2.count != 24 ...)` proves count *isn't* the wrong value, but doesn't prove it's the *right* one (0, unchanged). `assert(x2.count == 0 ...)` would be a tighter, more precise check of the actual claim being tested.

### Still outstanding
1. 2.3 warm-up — the explanation above is the correction; nothing further needed from you, just bank it mentally.
2. `std::adopt_lock` vs. `std::acquire_lock` — naming slip, noted.
3. Add `inline` to `fetch_price`/`fetch_all_prices` — quick, whenever convenient.

**4.2 (preamble + 4.2.1) is passable.** Next up: 4.2.2 whenever you're ready.

## 4.2.2 — Associating a task with a future

Triage: `std::packaged_task<>`'s signature-matching/conversion rule and its purpose-vs-`std::async` distinction are fact-shaped (quiz). The task-queue pattern Listing 4.9 derives in-text — decoupling "what to run + how to get its result" from "which thread runs it, and when" — is a reusable mechanism the same way 4.1's queue was, so it goes straight to a build task, not an English description of it. No warm-ups this round — nothing else came due at session 16 beyond the two already asked for 4.2's own worksheet.

Recall:

9. `std::async` and `std::packaged_task` both ultimately hand you a `std::future`. What does `std::async` decide for you that `std::packaged_task` deliberately leaves entirely up to you?

Answer: to be succinct about it, packaged_task allows the user more flexibility than async because with async, you cant decide when you run the callable and which thread runs the callable. With packaged_task<T> you can decide when to run the callable which has been wrapped by the packaged_task and you can also decide which thread will run the task, i.e., you can take the future associated with the packaged_task before moving it into the scope of the function which u want running the task and that function can be running on a thread of your choosing.

10. You can construct a `std::packaged_task<double(double)>` from a callable with signature `float(int)`. State the general rule this follows (not specific to this example), and identify the two places in the signature where it applies independently — the parameter side and the return side.

Answer: so the rule for passing callables to the packaged_task is - "as long as the types are implicitly convertible between eachother, the callable can be passed to the packaged task" - so this means that the given example in the question can work since we have double as the input and output type of the type defined for the callable being passed to the packaged task and this means that most functions with standard language numeric types can be passed so long as they have one input and one output parameter, i.e., int(int), double(int), float(float), these all are workable due to implicit casting among them however loss of information is possible.

**Correction - (2026-10-05):**

The above answer collapses into "numeric types are mostly interchangeable" rather than the actual two independent points. Stated precisely: the callable's *parameter* type must be implicitly convertible **from** what the packaged_task's declared signature will supply (here, `int` must be constructible from the `double` argument `operator()` will pass through), and the callable's *return* type must be implicitly convertible **to** the packaged_task's declared return type (here, `float` → `double` will work). These are two separate conversions, checked independently — a callable could satisfy one and fail the other. callable's parameter must be convertible **from** the declared signature type and callable's return type must be convertible **to** signature's return type.

Code-tracing (Listing 4.9):

11. In `gui_thread()`, the lock on the task queue's mutex is released *before* `task()` is actually invoked (comment `g` in the listing) — the lock is only held long enough to pop the task off the deque. Concretely, what goes wrong if that lock were instead held for the full duration of `task()`'s execution? Give the actual failure mode, not just "bad practice" — think about what `post_task_for_gui_thread` needs and what an arbitrary task body might itself do.

Answer: ok so the reason we want to keep the task() call outside the mutex is that the task itself might take a while to complete and the purpose of a mutex is to avoid race conditions over shared data resources, in this case the shared data resource is the tasks dequeue. The general rule to follow with mutexes is "minimize the number of lines protected by a mutex as much as possible" since the more lines are there under a mutex's scope - the more lines are serialized across the threads that share the callable and longer it takes for each thread to relinquish its lock over the mutex. As for specific failure modes of this particular situation, we could have a situation where the task itself blocks for a long time and meanwhile the tasks keep piling on in the tasks dequeue, in a system such as a market-data service, this could ultimately overwhelm the system's memory in a short period of time and cause a crash, had the task been outside the mutex and instead of one we had a pool of background threads then at least we'd avoid a system collapse, even if one thread blocks forever due to faulty code or data arrival in an unexpected format. A fringe but possible mode i can think of is if the task() itself needs the same mutex as the one held by the lock protecting the task dequeue in this consumer function, this is a deadlock situation, however, like i said its a fringe possibility since this mutex is meant to protect the task_queue itself and it shouldn't have tasks in it that are trying to act on their own queue.

Recall / connect:

12. Is `std::packaged_task` copyable? Answer precisely (yes/no) and connect it to the *same* rule you've now cited twice before — once for `std::future` itself (this section's preamble) and once for `std::async`'s move-only-callable case (4.2.1, Q6) — name what family of types this is and why a `packaged_task` specifically needs to belong to it (what would break if two copies of the same packaged task both existed).

Answer: No, the packaged task is not copyable - its movable and once a thread acquires a packaged task, no other thread can do that task, and thats the whole point of having it this way, packaged_task belongs to the family of move only types such as std::unique_pointer, std::future, std::thread, etc. having a copyable packaged_task wouldnt work because the result expected from get() on the other side with the future is also unique, i.e., we already know future is unique, and though shared_future is copyable, the underlying result is unique anyways. having a copyable packaged task wouldnt make sense for other reasons for example if there were copies of a packaged task being passed to different threads all associated to a unique future, then if the first one runs, what do the others do and further more, what about a race condition or overlap on them?, it just doesnt make sense design wise.

## Build task — `task_runner`

Directly reusable: a single background thread that executes posted tasks one at a time, decoupled from whoever's posting them, each posting call getting back a future for that specific task's completion — a minimal building block toward chapter 9's thread pool, and a real pattern for a trading system (e.g. a dedicated thread that handles logging, order submission, or any other work that specifically shouldn't run on a caller's own thread, without inventing a bespoke message type per kind of task).

One deliberate deviation from Listing 4.9, worth thinking about before you write anything: the book's `gui_thread()` loop polls (`get_and_process_gui_message()` every iteration, `continue` if the queue's empty) because it also has to poll for actual GUI messages — that's a real constraint of GUI event loops, not a recommendation. You have no such second polling need here. Given 4.1's Q1 (busy-polling costs) and its `wait_and_pop` design, the worker loop below should *block* when the queue is empty rather than spin — same predicate-wait shape as 4.1's queue, applied fresh to a new concrete case rather than reused as a dependency (4.1's `threadsafe_queue` target is being kept for its earmarked reuse point at 6.2/9.1, not linked in here).

Rough shape (fill in the actual design yourself):

```cpp
class task_runner
{
public:
    task_runner();   // starts the background worker thread
    ~task_runner();  // see shutdown contract below

    task_runner(task_runner const&) = delete;
    task_runner& operator=(task_runner const&) = delete;

    // Wraps f in a std::packaged_task<void()>, queues it for the background
    // thread, and returns its future. Accepts any no-argument callable
    // (bind/lambda-capture your way around needing arguments — this is
    // void() only, same scope as Listing 4.9's example, not a generalized
    // ResultType<T> version).
    template<typename Func>
    std::future<void> post_task(Func f);

private:
    // mutex + condition_variable-guarded deque<std::packaged_task<void()>>,
    // worker thread, and whatever stop-signaling state the shutdown
    // contract below needs.
};
```

Thread-safety contract: `post_task` may be called concurrently from multiple threads; the queue itself needs real protection under that contention, not just under a single caller. Only one worker thread ever pops/executes tasks, so tasks themselves never run concurrently with each other — don't add synchronization inside the tasks you test with on the strength of that guarantee alone, but don't rely on it either without being able to say *why* it holds (what specifically makes it true here).

**Shutdown contract (the genuinely tricky design decision here — get this exact, it's the point of the exercise):** the destructor must not block forever waiting for the whole queue to drain. On destruction: signal the worker to stop, let it finish the task it's *currently* executing (if any) to completion, but it must not start any task that was still sitting in the queue and hadn't begun running yet. Futures for those abandoned, never-started tasks should simply never become ready — don't manufacture a placeholder result or throw from destruction. Reason about this from first principles (what does the worker loop's predicate-wait need to check, and where does the stop signal need to be tested relative to popping vs. running a task) rather than guessing at an implementation.

Edge cases the driver needs to prove, concretely, not just "it compiled":

1. **Correctness + real contention on the queue itself.** Post a meaningful number of tasks (e.g. 20+) *from several concurrent caller threads*, not sequentially from `main` — the queue's own mutex needs to actually be exercised under contention, not just exist. Each task should produce a verifiable, order-independent side effect (e.g. append its own index to a shared, separately-protected result container). Wait on every returned future, then verify all tasks genuinely ran and produced the right result set. Since there's exactly one worker thread, execution order is deterministic FIFO by queue-arrival — decide whether that's worth asserting precisely, or just verify the full result set is correct.
2. **Tasks actually run on the background thread, not the caller's.** Capture `std::this_thread::get_id()` inside a posted task and confirm it's neither `main`'s id nor whichever caller thread posted it.
3. **The shutdown contract, exactly as specified above.** Post one slow task (sleep briefly so you can reliably observe it's "in flight") plus a couple more behind it in the queue, then let the `task_runner` be destroyed without calling `.get()`/`.wait()` on the later futures. Verify: the destructor returns promptly (doesn't hang waiting on the full queue), the in-flight task's side effect did happen, and the still-queued tasks' side effects did *not* happen.

ASan/UBSan clean (TSAN substitute per the usual sandbox limitation) across repeated runs, same bar as every prior build task. Remember `inline` on anything defined directly in the header outside the class body, if you end up with any free functions there — the class's own member functions don't need it (in-class definitions are implicitly inline).


Self-explanation before review (2-4 sentences, closed-book):
Answer: so I mixed unique_lock and condition_variable here to sleep the thread when its not working as good resource management instead of just blocking up the core, im particularly proud of understanding move semantics for this particular case, i.e., for non copyable lvalue associated packaged_task objects we need to move them into the containers. I am also pleased that I was able to judiciously use mutex and locks across all operations / methods for synchronized and thread safe operations on the dequeue. dequeue was used because it supports move()  and then popping the remaining lvalue at the front. the use of thread object as an internal object to the task_runner was not my idea however, i am particularly happy with how it turned out since this makes it a good candidate for something like a market-data layer, i.e., a market-data layer object instance with singleton pattern, can have its own thread pool as its member, i.e., a container of threads as part of the object and it can embed the order book with in itself or perhaps it can just have all market-data operations as part of itself along with its internal threads and it gets notified by a feed handler each time an update is made to the order book. the shutdown in the destructor is also a good idea in case its not called in the parent scope and the scope goes out.

## Review — 4.2.2

**Verdict: round 1, not yet passable.** The quiz is essentially strong, and — this is the important part — the actual `task_runner` *implementation* is correct: I verified its core claims independently with two scratch stress tests (not modifying your files), 200/200 and 30/30 trials respectively, both clean under plain and ASan+UBSan. The problem isn't the class, it's that two of your driver's three tests don't actually exercise the specific edge cases the spec named, so they don't prove what they claim to. Both are bounded, surgical fixes — no redesign needed.

### Quiz

Q9 — correct and precise: `std::packaged_task` leaves *when* and *on which thread* entirely up to you, `std::async` decides at least the "when" (and, absent an explicit policy, possibly the "whether it runs concurrently at all").

Q10 — the core rule (implicit convertibility) is right, but the answer collapses into "numeric types are mostly interchangeable" rather than the actual two independent points asked for. State it precisely: the callable's *parameter* type must be implicitly convertible **from** what the packaged_task's declared signature will supply (here, `int` must be constructible from the `double` argument `operator()` will pass through), and the callable's *return* type must be implicitly convertible **to** the packaged_task's declared return type (here, `float` → `double`). These are two separate conversions, checked independently — a callable could satisfy one and fail the other, and your answer doesn't have the vocabulary to say which one broke if it did. Small correction, not a redo.

Q11 — excellent, complete answer: correctly names the "minimize the critical section" principle, the queue-pileup/memory-pressure failure mode under a single worker with a slow task, and even flags the fringe self-deadlock case (a task that itself needs the queue's mutex) with the right caveat about why it's unlikely here. Nothing to add.

Q12 — fully correct: move-only, correctly named as the same family as `unique_ptr`/`future`/`thread`, and the reasoning for *why* (ambiguous ownership of the future's write-once result) is exactly right.

### Build — `task_runner`

**What's actually correct (verified, not assumed):** the shutdown contract is implemented exactly right — `to_shutdown` is checked immediately after `wait()` returns, before the queue is touched, so once `shutdown()` is called no task that hasn't already been popped will ever start; the currently-executing task (if any) is allowed to finish since it was already popped before the flag got checked. I wrote a scratch repro that posts tasks, calls `shutdown()` from a genuinely concurrent race window (not your driver's serialized get-then-shutdown pattern), and checks every future's `wait_for(0)` status afterward across 200 trials — `started_count` and `ready_count` matched exactly every single time, meaning no queued-but-unstarted task ever sneaks through. I also stress-tested `post_task()` from 8 concurrent producer threads (80 tasks total) across 30 trials, plain and ASan/UBSan — all results came back correct and unique, so the queue's mutex genuinely holds under real contention. No ODR issues either — everything's a class member, correctly implicitly inline; good, this is the first build since the `inline` slip last round, and it's clean.

**Two real gaps, both in the driver, not the class:**

1. **TEST_1 doesn't exercise edge case #1 as specified.** The spec explicitly asked for tasks posted *from several concurrent caller threads*, specifically so the queue's own mutex gets exercised under real contention — TEST_1 posts all 10 tasks sequentially from `main` in a single loop. This matters because a single-caller test can pass even if `post_task()` had a latent race that only shows up under concurrent callers (it doesn't, per my scratch test above, but your own driver doesn't prove that — it's currently taking my word for it, which isn't the point of the exercise). Fix: spawn several threads (4-8), have each post a chunk of tasks concurrently, join them, then collect/verify all the futures — same verification logic you already have, just with the posting loop moved into worker threads instead of `main`.

2. **TEST_3's assertion doesn't actually test the shutdown contract.** Look at the loop again: `results_arr_3[6..9]` never get a `.get()` call (the loop `break`s right after index 5), so those slots stay at their zero-initialized value *regardless of whether task_runner ever ran tasks 6-9 or not* — `count_results_extracted == 6` is guaranteed by your loop's own control flow, not by anything `task_runner` does. You actually had the right instinct — the commented-out `wait_for(...) == std::future_status::ready` check and the disabled `count_futures_ready_not_extracted` assertion were exactly the right approach, just not finished. Uncomment that, sleep briefly after `shutdown()` to let things settle, then assert on the *abandoned* futures' `wait_for(0)` status directly (should all report `std::future_status::timeout`, i.e., never became ready) rather than inferring it from what `results_arr_3` never got the chance to record.

**Minor, not blocking:**
- `get_run_task_thread_id()` returns `std::this_thread::get_id()`, which reports whichever thread *calls* this function — from `main`, that's `main`'s own id, not the worker's. It's unused in your driver (TEST_2 correctly gets the worker's real id the only way that actually works, by capturing it from inside a posted task), so it's dead rather than actively wrong, but worth either fixing (the worker thread would need to record its own id into a member on startup) or removing.
- Interface note, not a bug: the spec's rough shape used `std::packaged_task<void()>` (explicitly scoped to `void()`, not a generalized `ResultType<T>`); you built `int()` instead, which is a reasonable, deliberate widening (lets tasks return a value, which your tests then use for verification) rather than a miss — just flagging the deviation per the framework's rule that any deviation gets called out as fine-or-bug. This one's fine.
- Worth affirming, not fixing: deleting the move constructor/assignment ("FOR NOW" in your comment) is actually the *correct* call here, not a limitation — `runner_thread` is constructed with `&task_runner::run_task, this`, so a moved-from `task_runner` would leave the background thread calling member functions through a stale `this`. Good instinct to lock that down rather than leave it implicitly deleted by accident.

### What's needed to close this out
Two driver fixes — TEST_1's concurrent producers, TEST_3's real `wait_for`-based abandoned-future check — re-verified (plain + ASan/UBSan, several runs). No changes needed to `task_runner.hpp` itself. Once those land, this is done.

### Round 2

TEST_1's concurrent-producer fix is correct (4 threads, disjoint index ranges, no shared-write races). TEST_3's `wait_for`-based check was right in structure but the first version asserted `count_futures_ready_not_extracted == 1` — too strong: whether the worker or `shutdown()` wins the race for the mutex right after task 5 completes is genuinely unsynchronized (nothing forces the worker to grab task 6 before `shutdown()`'s lock_guard lands), so 0-in-flight is a legal outcome too, not just 1. Caught this empirically — 1 failure in ~110 runs under ASan, none in 80 plain runs, exactly the kind of rare-but-real race that only shows up under sanitizer-shifted scheduling. Fixed to `<= 1`; re-verified 20/20 ASan+UBSan clean on the corrected version, on top of the round-1 verification (200/200 and 30/30 scratch trials) that already confirmed the class itself was never the problem.

**Verdict: passable. 4.2.2 closed.**

### Erratum (2026-09-24, framework migration audit): the shutdown contract in the spec above was wrong

The spec said futures for abandoned, never-started tasks "should simply never become ready". That's only true **while the `task_runner` is still alive**. Once it's destroyed, its `std::deque<std::packaged_task<int()>>` is destroyed too, and destroying an unrun `packaged_task` stores a `std::future_error` (`broken_promise`) in its shared state. The future becomes ready, and `get()` throws. This is the same mechanism as destroying an unset `std::promise` (4.2.3, TEST_2).

Verified with a scratch test against `task_runner.hpp`: the queued task's future was not ready after `shutdown()` but before the destructor, was ready after the destructor, and `get()` threw "Broken promise". Your TEST_3 checks the futures *before* destruction, so it's still correct. Only the spec's wording was wrong. Now review-queue item `4.2-abandoned-task-futures`.

## 4.2.3 — Making (std::)promises

Triage: `std::promise<T>`'s contract (`get_future()`, `set_value()`, destroy-without-setting behavior) is fact-shaped, and now that three future-producing mechanisms are live (`std::async`, `std::packaged_task`, `std::promise`), the genuinely valuable question is discrimination between them — quiz territory, no build needed to answer "which one fits here." But Listing 4.10's actual pattern — a single thread demultiplexing many concurrent outstanding requests by ID, each caller getting a future keyed to *their* request — is a reusable mechanism in the same sense as 4.1's queue and 4.2.2's task_runner: directly the shape of a persistent exchange WebSocket connection handling many in-flight requests (place-order acks, book snapshots) tagged by a client-assigned correlation ID. That gets a build task. No warm-ups this round — nothing new came due at session 16 beyond what 4.2's own worksheet already used.

Recall:

13. `std::promise<T>` and `std::packaged_task<T()>` both give you a `std::future<T>` via `get_future()`, and both let something other than the future's owner control when it becomes ready. What's the one structural thing `std::packaged_task` wraps that `std::promise` deliberately doesn't — and given that, when would you reach for a bare `std::promise` instead of wrapping the same logic in a `std::packaged_task`?

Answer: so with std::packaged_task, we can decide which thread runs the task and when that task is run using say a queue with a notify() condition_variable which then can wake up the thread or thread pool responsible for executing the packaged tasks. But suppose that we are in a situation where the control of when the event might happen is not internal to the application and not triggered by our own hands or logic but rather is reliant on external system, this means that the result that needs to be stored in the future is not reliant on the return value of function that we wrote in the code, the value or things that make the value that needs to be stored comes from outside the code, this means that there needs to be a way to just store the value of the future manually across threads and std::promise is that mechanism. It allows us to store the result value of a future and make it ready, where (which thread) we want, when we want (when the external value or event becomes available) and also however we want, i.e., we dont need a callable sent to the promise for the result to be stored in the future, we can just place the value directly based on what comes from outside and this makes the difference because with packaged_task the rigidity comes from the fact that we can only supply packaged_tasks of a particular type in a queue or shared container to other functions and though there can be workarounds for this like just relying on side-effects while have a vague void() as the template function parameter, however, instead of trying to work around such a limitation, we can just use std::promise which allows for us to just set a value from another thread and what we need for making promises work is to have them stored in a map or some other shared data structure such that when external data arrives, its appropriately processed and then the result value can be stored in the promise and thus make the future ready.

14. The text states: "If you destroy the `std::promise` without setting a value, an exception is stored instead." Precisely: stored *where*, and what does that mean concretely for a thread that's already blocked on `.get()` of the associated future when this happens — does it hang forever, or something else?

Answer: the exception is stored in the future in place of the result or expected value if the promise is destroyed before being set. so .get() blocks in hte situation where the promise hasnt been set with the result and also hasnt been destroyed however if it does get destroyed or set an exception imn place of the value then that exception is then rethrown in the scope of the .get() and we can handle it using try-catch or we can let it wreak havoc but thats what happens. the future doesnt block forever if the exception has been stored, rather it throws immediately if it was being waited or blocked on using get().

Code-tracing / connect (Listing 4.10):

15. `process_connections()` handles a potentially large number of network connections from a *single* thread, using one `std::promise` per in-flight request/response rather than spawning a thread per connection. Connect this directly to 2.4's material: what specifically goes wrong, resource-wise, once the number of connections (and thus the number of threads, in a thread-per-connection design) climbs past what 2.4 called the hardware concurrency limit — and why does moving to "one thread, N promises" sidestep that specific cost rather than just relocating it?

Answer: So, first we need to address what threads are - threads are a concept on an OS level which represent a bunch of resources like time on a CPU core, and memory or cache in addition to time on the core to execute a certain task like a callable. depending on how many cores you have on the computer CPU, there is a limit to how many tasks you can actually run concurrently on that CPU, i.e., on a 16 core CPU like this one, apart from the OS tasks, say you have 12 cores to user disposal so at a time, a user codebase can truly run 12 tasks simultaneously, i.e., you can have 12 while true functors blocking 100% of those cores. That being said, such while loops are usually not used and threads even if persistant sleep to give way to other threads to acquire time on the core, and this switching of threads on cores allows for more threads than cores. a good rule of thumb is to have no more than two threads per core in the overall scheme of the application. So the reason this is the case is that each time we switch threads on a core, we face whats called task switching overhead and this means a small slice of time lost as tasks are switched and old thread state is saved and put aside if needed to make way for the new task thread. Too much task switching overhead caused by too many threads can lead to reduced performance and serialization of a supposedly concurrently code base due to hardware limitations. beyond this, there are of course possible issues with RAM as more threads are pile on and cache lines (ive heard those words) however, teh concrete knowledge I have gleaned so far in this regard is written above and is the extent of whats been taught in this book to my honest knowledge. there is function `std::thread::hardware_concurrency()` which gives a ballpark estimate of how many threads you can truly run concurrently on your system and its best if you remain below or at this count or at least no more than twice this count at maximum in your application.

## Build task — `pending_requests`

Directly reusable: the exact shape of Listing 4.10's connection-handling pattern, generalized — a single receiving thread (or callback) fulfills requests by ID as responses arrive, in whatever order they actually arrive, while any number of callers each hold a future keyed to their own outstanding request.

Rough shape (fill in the actual design yourself):

```cpp
template<typename Key, typename Value>
class pending_requests
{
public:
    // Registers a new outstanding request under `id`, returns the future
    // the caller will block on for the eventual response. Safe to call
    // concurrently from multiple caller threads registering different ids.
    std::future<Value> register_request(Key const& id);

    // Called by the (single, in this build's scope) receiving side once a
    // response for `id` has arrived. Makes register_request()'s returned
    // future ready with `value`.
    void fulfill(Key const& id, Value value);

private:
    // mutex-protected map<Key, std::promise<Value>>. fulfill() should
    // remove the entry once resolved -- a promise is one-shot, same as a
    // packaged_task.
};
```

Thread-safety contract: `register_request()` may be called concurrently from several caller threads registering *different* ids — the underlying map needs real protection under that contention, not just under a single caller (same lesson as 4.2.2's TEST_1 gap — apply it from the start this time). This build's scope assumes a single thread calls `fulfill()` (mirroring Listing 4.10's single connection-processing thread), so `fulfill()` calls don't need to be safe against each other — only against concurrent `register_request()` calls touching the same underlying map.

What `fulfill()` should do with an `id` that was never registered, or was already fulfilled, is out of scope for this build — treat it as a precondition the caller must uphold (same category as calling `.get()` twice on the same future), not something you need to handle gracefully.

Edge cases the driver needs to prove, concretely:

1. **Correctness under real concurrent registration, out-of-order fulfillment.** Register a meaningful number of requests (15-20+) from several concurrent caller threads (not sequentially), each getting back its own future. From a separate thread, fulfill them in an order that's deliberately *not* the registration order (mirrors the text's "packets arrive in essentially random order"). Verify every future's `.get()` returns the value matched to *its own* id, not some other request's.
2. **The abandoned-promise behavior is real, not assumed.** Register a request, deliberately never fulfill it, then let the `pending_requests` object (and the still-pending `std::promise` inside it) be destroyed. Verify the caller's `.get()` on that future actually throws (catch it) rather than hanging forever — this is the text's own stated claim from this section, worth proving empirically rather than taking on faith.

ASan/UBSan clean across repeated runs, same bar as every prior build task. Everything here is a template class defined in-class, so no `inline` concerns — but if you add any free functions at namespace scope in the header, they need it.

Self-explanation before review (2-4 sentences, closed-book):
Answer: I am happy with how this turned out - i did think it prudent to use lock in fulfill even if one thread is using it since find could get corrupted values when insertion is happening on registering function or it could mess up the things while erasing for registering threads by breaking invariants. furthermore, i first though about moving the promise out of the map and then fulfilling them in the scope but i think just setting them and disposing off them in the map along with the entry is good because they outlive their utility as soon as they are set and it matters not where they are set but it does matter where the future is at which is being set and that too in the context of the scope it lies in. promises are quite flexible if the problem has events that arent happening from our own agency or codebase and are on an unpredictable time schedule.

## Review — 4.2.3

**Verdict: round 1, not yet passable.** Quiz is strong. `pending_requests.hpp` itself is correct — verified independently with a scratch stress test (genuinely concurrent registration threads, shuffled fulfillment order, strict per-key value checks, 30/30 trials, plain + ASan/UBSan). The gap is in TEST_1, and it's worth being direct about it: it's the same category of thing round 1 caught in `task_runner` — the test looks concurrent but doesn't actually exercise the property the spec named.

### Quiz

Q13 — excellent. Precisely identifies that `packaged_task` wraps a callable (the result comes from code you run), while `promise` is for values that originate outside any callable you control — and connects it back to why forcing external, event-driven data through a fake `void()` callable would be an awkward workaround rather than the right tool. Nothing to correct.

Q14 — excellent, precise on both halves: exception stored in the future's shared state, and a thread already blocked in `.get()` doesn't hang — it throws immediately once the promise is destroyed unset. Nothing to correct.

Q15 — excellent, and this is the standout answer of the section: correct on thread/core costs, context-switching overhead, `hardware_concurrency()` as the practical guide.

### Build — `pending_requests`

**What's correct (verified):** the class itself — locking discipline in both `register_request()` and `fulfill()`, `get_future()` called before the promise is moved into the map (correct — the shared state travels with the move, not tied to the promise's current address), erasing the promise right after `set_value()` (safe — the future holds its own independent reference to the shared state, doesn't care that the promise object is gone). I stress-tested it separately: 20 concurrent registration threads, fulfilled in a shuffled (non-registration) order, checked each future's value against its *specific* key — 30/30 trials clean, plain and ASan/UBSan.

**TEST_1 doesn't actually prove what it claims to, in three connected ways:**

1. **Not genuinely concurrent.** Each loop iteration calls `std::async(...).get()` *in the same iteration*, before moving to the next `i`. `std::launch::async` does start a real thread, but blocking on `.get()` immediately means each registration finishes before the next one starts — this is sequential registration wearing a concurrency-shaped costume, not actual concurrent contention on the map. Compare to your own `task_runner` TEST_1 fix last round, which got this right: spawn all the threads, *then* join them. Same fix applies here — launch several threads that each register a chunk of requests, join them all, *then* proceed.
2. **Fulfillment order matches registration order.** Both loops go `0..14` over the same `keys` array — this never exercises the "arrives in whatever order it actually arrives" case the spec named (and Listing 4.10 itself describes). Shuffle the fulfillment order.
3. **The correctness assertion is permutation-blind.** It checks that every returned string exists *somewhere* in the full expected set (`test_tall_map_reverse.count(num)`) — it doesn't check that `string_futures_array[i]`'s value is *specifically* the one registered under `keys[i]`. A bug that mixed up which id got which value (fulfilled the wrong map entry, say) would pass this test as long as the multiset of values came back complete. Assert `string_futures_array[i].get() == values[i]` directly instead.

None of these are code bugs — `pending_requests` already handles all three correctly, per the scratch test above — but the driver needs to actually demonstrate that, not take it on faith. All three are small, contained fixes to TEST_1 alone.

**TEST_2 is correct as-is** — nice, clean proof: separate instance, register-without-fulfill, destroy the object in its own scope, `.get()` outside that scope throws, caught and asserted. Exactly what the edge case asked for, no notes.

### One thing on my end, not yours
You're right that I dropped two things from the framework the last two rounds: I didn't include the "self-explanation before review" prompt in either 4.2.2's or 4.2.3's build spec (you caught it and wrote one anyway both times — appreciated, and it should've been asked for, not volunteered), and I skipped warm-ups for both worksheets on the reasoning that "nothing was numerically due" — but the framework's own spacing rule has a third tier (cross-chapter design-judgment questions, once ≥2 mechanisms are live) that isn't due-in-gated at all and should fire regardless. Both are fixed starting with whatever's next — self-explanation prompt back in the build spec, and a warm-up (misconceptions log → due items → cross-chapter judgment, in that priority order) opening every new worksheet.

### What's needed to close this out
TEST_1's three fixes (concurrent registration, shuffled fulfillment order, per-key assertion), re-verified plain + ASan/UBSan. No changes needed to `pending_requests.hpp`.

## 4.2.4 / 4.2.5 — Saving an exception for the future / Waiting from multiple threads

Triage: both subsections are fact/gotcha-shaped — exception propagation across the three future-producing mechanisms, a real book-vs-current-standard-library trap, and `shared_future`'s genuinely counter-intuitive non-thread-safety. Nothing here needs a new class built to master it (the "mastery" is knowing the contract and not getting burned by it, not building a data structure) — quiz-only, no build task this round.

Recall:

17. `std::async` and `std::packaged_task` both automatically capture an exception thrown by the wrapped callable and store it for `get()` to rethrow — no extra code needed from whoever wrote the callable. `std::promise` doesn't have a callable to automatically catch anything from. What do you have to do differently when using a bare promise to get the same effect, and name the two ways the text gives you to do it.

Answer: std::async and std::packaged_task both store the error in the future so that .get() when blocks, receives the error which we can handle in a try-catch. std::promise has a slightly different method where since we manually set the value using set_value(), we can use if-else or try-catch on the side where we are going to fulfill the promise to use set_value() or set_exception() functions to set the value or exception, respectively, in the corresponding future. If we're using try-catch, to supply the same exception caught in the catch we can use std::current_exception() like this:

```cpp
    try{
        some_promise.set_value(calculation());
    }catch(...){
        // to send the current caught error
        some_promise.set_exception(std::current_exception());
        // to send a particular class of errors
        some_promise.set_exception(std::copy_exception(std::logic_error("something something_else")));

    }
```

18. The book's own code sample for this section uses `std::copy_exception()` as "the cleaner alternative" to a `try`/`catch` + `std::current_exception()` block. If you typed that into a real, current compiler today, what would happen — and what's the actual name of this function in the standard as it shipped?

Answer: this is now an outdated function std::copy_exception on the compilers today would not compile because std::copy_exception is not standard library function. the actual modern code for doing this is:

```cpp
some_promise.set_exception(std::make_exception_ptr(std::logic_error("foo")))

```
copy_exception and make_exception_ptr - both are doing the same thing, i.e., creating an exception pointer from an exception object without throwing it.

19. Back in 4.2.3 (Q14) you correctly identified that destroying a promise without setting a value makes a blocked `.get()` throw immediately rather than hang — but at the time, the *specific* exception type wasn't given. Now it is: what's the precise exception type and error code, and does this match what you actually observed empirically when you tested this exact scenario in `pending_requests`' TEST_2?

Answer: yes it does match what i observerd in test 2 in the cout statement i fortunately left there instead of just doing the assert. the `std::cerr << e.what() << '\n';` showed a broken_promise in the terminal. now in the code, with an unfulfilled and destroyed promise, we have stored in the .get() a std::future_error with an error code of std::future_errc::broken_promise.

Gotcha (the centerpiece of 4.2.5 — this one genuinely burns people):

20. `std::shared_future` is copyable, unlike `std::future` — it's tempting to conclude from that alone that `std::shared_future` is therefore safe to use from multiple threads. Precisely: is a single `std::shared_future` *object* safe to call `.get()` on concurrently from two different threads? If not, what's the actual safe pattern the text describes, and why does `shared_future` being copyable matter for that pattern specifically (not just "because it's shared_future")?

Answer: Yeah, had to sit on this one for a while myself, what i have been able to surmise is that although we have made a shared_future object using this line here `std::future<int> f(p.get_future());` that still doesnt mean we have a local copy of the shared future, which means that if multiple function calls happen to the callable which contains this snippet:

```cpp
std::promise<int> p;
std::future<int> f(p.get_future());
std::shared_future<int> sf(std::move(f));
```
sf is still the same object for all calling threads and can cause a race condition if called .get() on. however, we know that function local varibles are thread local, i.e., if defined something like this:

```cpp
std::promise<int> p;
std::future<int> f(p.get_future());
std::shared_future<int> sf(std::move(f));
auto sf_local = sf;
```
now sf_local is a local variable to the thread of the callable with the above lines and each thread's callable can safely call .get() on their on sf_local and not cause a data race since in the scopes of their own callable copies, sl_local are local objects. 

**Correction - (2026-10-05):**

a single `std::shared_future` object is **not** safe to access concurrently from multiple threads. The important distinction is between the **`shared_future` objects themselves** and the **shared result state** they refer to. If multiple threads have access to the same `shared_future` object and call `.get()` on it concurrently, that object itself needs synchronization.

The safe pattern is to give each thread its **own copy** of the `shared_future`. This is where copyability specifically matters: `std::future` is move-only, so you cannot create multiple independent `future` objects referring to the same shared state. `std::shared_future` is copyable, so you can give each thread its own object while all copies still refer to the same result.

```cpp
std::promise<int> p;
std::future<int> f = p.get_future();

std::shared_future<int> sf = f.share();

std::thread t1([sf]() {
    int result = sf.get();       // t1 has its own shared_future object
});

std::thread t2([sf]() {
    int result = sf.get();       // t2 has its own shared_future object
});

std::thread t3([sf]() {
    int result = sf.get();       // t3 has its own shared_future object
});

p.set_value(42);

t1.join();
t2.join();
t3.join();
```

Here, the lambda capture `[sf]` **copies** the `shared_future` into each thread's callable. The three objects are independent, but they all refer to the same underlying shared result. That's precisely what `shared_future`'s copyability enables.


Code-tracing (the `valid()` example):

21. 
```cpp
std::promise<int> p;
std::future<int> f(p.get_future());
assert(f.valid());
std::shared_future<int> sf(std::move(f));
assert(!f.valid());
assert(sf.valid());
```
Walk through precisely why `f.valid()` becomes false after constructing `sf` from it — tie this to the same ownership model you already used to explain `std::future`'s `unique_ptr`-like behavior back in the 4.2 preamble. What's the difference between this explicit `std::move` constructor and calling `.share()` on a future directly — is there an actual behavioral difference, or just a spelling one?

Answer: we know that to move a unique_ptr from one lvalue to another, be it in another scope or not, we need std::move() and when we do so, the unique_ptr lvalue we moved the rvalue out of now has a nullptr when we call .get() on it. Very similar to this, the std::future has a .get() method as well as the same move semantics, i.e., we can only move the ownership of a future, we cannot copy it. when we want to move the rvalue of std::future from one future to another, we use std::move(); however, when we call .share to make a std::shared_future or move it to a shared_future object constructor, we are still taking away the ownership of the std::future object away from it and into a shared_future object and so the std::future after std::move into a shared_future or a .share() is gonna be invalid. there is no behavioral difference, just there is a potentially less typing advantage when using share() for not having to type the type again explicitly, as shown below:

```cpp
std::promise< std::map< SomeIndexType, SomeDataType, SomeComparator,
SomeAllocator>::iterator> p;
auto sf=p.get_future().share();
```

## Review — 4.2.4 / 4.2.5

**Strong session overall — one real precision gap (Q20), everything else correct.** No build task this round, nothing to compile or verify; this is a quiz-only close.

Q17 — correct on both counts: `get()` rethrows automatically for async/packaged_task, and the two promise-side methods (`current_exception()` in a catch block, `copy_exception()`/`make_exception_ptr()` directly) are both named accurately.

Q18 — correct: won't compile, `std::make_exception_ptr()` is the real name. One optional bonus fact, not a correction: the rename wasn't just deprecation-for-its-own-sake — LWG issue 1130 flagged `copy_exception` specifically as a *misleading* name, since it sounds like it copies an existing `exception_ptr` when it actually constructs a brand new one from an exception object. Not required knowledge, just the "why" behind the "what."

Q19 — correct, and this is exactly the kind of connection the framework's built for: you tied the abstract error code straight back to the literal `std::cerr` output your own `TEST_2` printed. That's the loop closing for real, not just in theory.

Q20 — the conclusion (need a per-thread copy, not shared access to one object) is right, but two things need tightening:
- The setup describing what's actually *unsafe* doesn't quite hold up: a snippet with `sf` as a plain local variable, called multiple times, doesn't create a race — each call gets its own independent `p`/`f`/`sf`, nothing is shared between calls at all. The actual danger scenario is when *one* `shared_future` object is reachable from more than one thread simultaneously — e.g. a class member, a global, or a variable captured *by reference* (`[&sf]`) into multiple thread lambdas — and more than one of those threads calls `.get()` on that same object concurrently. That's the case your `sf_local` fix actually solves; the illustration just needs to set up the danger correctly first.
- The question specifically asked *why copyable-ness matters* for the fix, not just that copying works. Precisely: `std::future` is move-only, so if it were future you were dealing with, you fundamentally *couldn't* hand out three independent objects referencing the same state — there'd only ever be one `future` object in existence for that state, full stop, which is exactly the "only one thread can wait" limitation the chapter opened 4.2.5 with. `shared_future` being copyable is what makes "give each thread its own object" possible as a fix *at all* — without that, the per-thread-copy pattern wouldn't be an available option, not just a better one.

Q21 — correct and precise: the `unique_ptr` analogy carries through cleanly, and the `.share()` vs. explicit-move distinction (typing convenience only, no behavioral difference) is exactly right, backed by the actual verbose-type example from the text.

**4.2.4 / 4.2.5 closed.** Only open item is Q20's precision correction above — nothing to redo, just worth having landed before it comes back as a warm-up.

### Correction check (2026-10-06)

These corrections were written after the review, so they don't count as retrieval and the queue schedule is unchanged. The queue items' `source` lines now point here. 4.2.1 Q2, 4.2.2 Q10, 4.2.5 Q20: correct.
