# 5.3 — Synchronizing operations and enforcing ordering

## 5.3 intro, 5.3.1–5.3.2 — Listing 5.2; the synchronizes-with relationship; the happens-before relationship

```
$ python3 tools/review.py due   (2026-09-28)
Due now: 22 active (15 overdue) | maintenance due: 0 | due in next 3 days: 24 | total active: 65, retired: 1
WARM-UP PICKS (3):
  1. [miss] 2.5-thread-id-ordered-vs-hash  (sec 2.5, 2d overdue, streak 0)
  2. [miss] 4.3-timeout-return-types  (sec 4.3, 2d overdue, streak 0)
  3. [miss] 5.2-cas-expected-overwrite  (sec 5.2, 2d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.3 Q1
Also due (next worksheets): 5.2-cas-failure-ordering, 5.2-cas-update-loop, 5.2-memory-order-by-operation,
  3.3-map-concurrent-write-unsafe, 4.2-packaged-task-conversion-rule, 4.2-shared-future-per-thread-copy,
  5.1-memory-locations-bitfields, 4.2-future-mechanism-discrimination, 5.2-atomic-flag-special,
  5.2-atomic-flag-static-init, 5.2-last-one-out-counter, 5.2-rmw-result-not-reload, 3.2-deadlock-both-locks-held,
  4.1-spurious-wake-predicate, 4.2-launch-policy-default, 5.2-atomic-bool-vs-flag, 5.2-read-mostly-lock-choice,
  2.2-conversion-in-new-thread, 3.3-dclp-visibility
Open fix-list items: 10 (F1-F10)
Lint: OK
```

Triage:
- Listing 5.2's chain (write data, store flag, load flag reads `true`, read data), and the fact that the synchronizes-with link exists **only when the load reads the stored value**: **code-tracing** (Q1).
- The three kinds of write a load can read from and still synchronize-with W (W itself, a later atomic write by W's thread, a chain of read-modify-writes that started from W's value): **code-tracing** (Q2).
- Synchronizes-with comes only from atomic operations; a mutex gets it from the atomic operations inside it: **recall** (Q3).
- Single-thread sequencing: operations in one statement are generally unordered (Listing 5.3), with the comma-operator and nested-call exceptions; statements are ordered: **code-tracing** (Q4).
- Inter-thread happens-before and its two combination rules with sequenced-before ("one synchronizes-with is enough for a whole batch of writes"): covered inside Q1(a); not quizzed separately.
- **No build.** Listing 5.2 is the flag-publish pattern. With the default ordering it's two lines, and nothing about it is new code beyond `spinlock_mutex`/`snapshot_cell`. It becomes a real mechanism once 5.3.3 introduces the ordering tags, so the build decision is deferred to that worksheet. Say so if you want one now.
- "Suitably tagged" and the data-dependency nuances are explicitly deferred by the text to 5.3.3/5.3.4. Not quizzed.

Queue note: the backlog is 22 due, but nothing is more than 3 days overdue, so the framework's review-only trigger (items more than 7 days overdue) isn't met yet.

### Warm-up

1. A trading system keeps per-worker counters keyed by `std::thread::id`. Version A uses `std::map<std::thread::id, Counters>`, version B uses `std::unordered_map<std::thread::id, Counters>`. For each version, which facility of `std::thread::id` makes it compile, and which of the two facilities does that container never touch? Apart from keying a container, name one thing you can do with a set of ids *only* because the comparison operators give a total order.

Answer: std::map is ordered and so the ability of std::thread::id to have a complete set of comparison operators allows for objects of this type to be used as keys in associative containers like std::map or be sorted or compared among themselves. the standard library also offers hashing function for std::thread::id, std::hash<std::thread::id> which allows for the use of std::thread::id as keys in non-associative containers as well. AS already mentioned, std::thread::id has a full suite of comparison operators which allows for sorting among other comparison based logic which could potentially allow for threads to be assigned work more selectively in a shared function and can also dry up code for shared callables.

2. Give the exact return type, and the values it can take, for each of these:

```cpp
cv.wait_until(lk, deadline);
cv.wait_until(lk, deadline, [&]{ return !q.empty(); });
fut.wait_until(deadline);
```

   Then: an order-ack handler polls in a loop with `if (fut.wait_for(50ms) == std::future_status::ready) { handle(fut.get()); break; } else { log_slow_ack(); }`. The future came from `std::async(std::launch::deferred, fetch_ack)`. What does the loop do, and why?

Answer:  first the types part of the question:
```cpp
cv.wait_until(lk, deadline);
```
this will take a time point value in deadline, a unique_lock in lk and will return cv_status::timeout/cv_status::no_timeout based on reason for its wake, i.e., timeout or notification.
```cpp
cv.wait_until(lk, deadline, [&]{ return !q.empty(); });
```
this one will take unique_lock in lk, and a time_point in deadline and for a return type it will return a bool representing the return type of the predicate which one can then use to surmise if it was a notification+predicate or a timeout that woke it.

```cpp
fut.wait_until(deadline);
```
this one will take a time_point as input and will return future_status::ready/future_status::timeout/future_status::deferred based on wether the future has been set, not set yet and the deadline was hit or the execution policy was std::launch::deferred for std::async.

For the next part, we discuss the loop setup for receiving the acknowledgement message of an order placed. Essentially, we have a `while` loop that checks the future returned by `std::async(std::launch::deferred, fetch_ack)`. However, because the launch policy is `deferred`, `fetch_ack()` does **not** run in another thread. `fut.wait_for(50ms)` will simply return `std::future_status::deferred` rather than waiting for the acknowledgement or starting the function, so the loop repeatedly takes the `else` branch and logs a slow acknowledgement. The deferred `fetch_ack()` only actually executes when an operation such as `fut.get()` requires its result. Therefore, if the intention is to run `fetch_ack()` concurrently while polling its future every 50 ms, `std::launch::async` should be used instead. With `async`, `wait_for()` can return `ready` when the acknowledgement arrives or `timeout` when 50 ms passes without it.



3. `std::atomic<bool> busy{false};` guards the one rebalancing job allowed at a time; the holder stores `false` when it finishes. R1 is mid-rebalance (`busy == true`). R2 runs:

```cpp
bool expected = false;
while (!busy.compare_exchange_strong(expected, true)) { }
rebalance();              // R2 believes it now holds busy
busy.store(false);
```

   (a) Trace R2's first two `compare_exchange_strong` calls: `expected` and `busy` before and after each, and the return value. Does R2 rebalance while R1 still is?
   (b) Write just the claiming code for two functions: `bool try_claim()` (one attempt, returns whether this thread won) and `void claim()` (waits until R1 releases, then takes it).

Answer: ok for this to work as intended, i.e., only one thread running `rebalance()` at a time, we need to ensure that `busy` is false first before being made true again and the while loop getting crossed, i.e., this code should have resetting of `expected` to false in the while loop body. looks like this:

```cpp
bool expected = false;
while (!busy.compare_exchange_strong(expected, true)) {
    expected = false;
}
rebalance();              
busy.store(false);
```
ok now for the trace, say R1 is the thread thats now at and running `rebalance()` and we have R2 in first run of this function and it hits the CAS operation, it sees expected is false and busy is true and doesnt match so the CAS returns false and that means while(true) so the CAS sets `expected = true` as well and the loop then reruns with `expected = true`, this second time though the expected is true and busy is true and now even though the R1 is not done with rebalance(), R2 shows up and runs it too leading to a bad race on that line. the solution described above with resetting expected in the while loop body prevents this scenario.

### Quiz

1. (Code-tracing) One `feed` thread and one `strategy` thread start at the same time. Default ordering everywhere.

```cpp
struct Quote { std::int64_t bid, ask; };
std::vector<Quote> book;                 // plain, not atomic
std::atomic<bool> ready{false};

void feed() {
    book.push_back({100, 101});          // (1)
    ready.store(true);                   // (2)
}

void strategy() {
    bool r = ready.load();               // (3)
    std::size_t n = book.size();         // (4)
    if (r && n > 0) use(book[0]);        // (5)
}
```

   (a) In a run where (3) reads `true`: give, link by link, the relations that make (1) happen-before (4). Name each link (sequenced-before, synchronizes-with, inter-thread happens-before) and the rule from 5.3.2 that lets you join each pair of links.
   (b) In a run where (3) reads `false`: is (4) a problem? Say exactly which relation is missing and what the book says the result is.
   (c) Swap lines (3) and (4), so `book.size()` runs first. Is the program now OK in runs where the load reads `true`? Why or why not?

Answer: 
(a) in this above code example, we have one producer and one consumer thread, so we are not going to be considering issues potentially caused when we have multiple threads racing through the different statements of feed() and strategy() functions. with that said, (1) is sequenced before (2) so (1) happens before (2). (2) is a store operation and store operations are what create synchronizes with while (3) is a load where load operations are what allow participation in synchronizes with and so we get (2) synchronizes with (3) set up which means (2) happens before (3) across threads. Finally, we have (3) sequenced before (4) so (3) happens before (4). we know happens before is `transitive` which means according to above description (1) happens before (4).

(b) I think that in case of there being two threads - one producer and one consumer, there isnt any issue of operation reordering happening since we have default seq_cst memory order with the (3) load which avoids the size read or book[0] access operations beeing reordered above (3) and since there is only one consumer thread, (4) and (5) being reordered among each other (unlikely) still wont cause any issue. the problem i see is the missing while loop which ought to be there to block the consumer thread for it to catch the ready flag's flip, such that we ought to have this:

```cpp
void strategy() {
    while(!ready.load()){ // (3)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }               
    std::size_t n = book.size();         // (4)
    if (r && n > 0) use(book[0]);        // (5)
}
```
this, is needed to ensure the operations (4) and (5) actually  end up running downstream. with that said, we could dispense with the sleep command but thats besides the main objection i wanted to raise.

(c) I want to say yes for this instance since the line (4) is still sequence before its check in (5) which makes it ok for when there is one thread, however, it does feel shaky since it is now open to being reordered and is out of the anchor scope of the atomic operation ` bool r = ready.load(); ` which prevents reordering so the condition might run before the actual command for size() runs due to reordering however im pretty sure of this being highly unlikely due to the happens before instituted because (4) being above (3) would still make it sequenced before (5).


2. (Code-tracing) Default ordering everywhere. Each thread runs its lines once, in the order shown.

```cpp
std::atomic<int> x{0};
int payload = 0;                         // plain

// Thread A              // Thread B            // Thread C
payload = 7;             x.fetch_add(10);       x.store(5);
x.store(1);   // W
x.store(2);

// Thread R
int v = x.load();
```

   For each value R's load might return, **v = 1, 2, 11, 5, 15**, does R's load synchronize-with W (so R could safely read `payload` afterwards through W)? For each "yes", name which of the three cases in 5.3.1 applies. For each "no", say why none of them applies.

Answer:
(a) for v = 1, this means that x.load() synced with x.store(1) so in this case - yes - R could safely ready payload. this is where we have sequenced before and synchronized with cases which allowed payload = 7 to happen before x.load().
(b) for v = 2, similar case to (a) where now instead of binding to store(1), x.load() was bound to x.store(2) and payload = 7 and x.store(1) happened before that thanks to sequenced before, so - yes - R will be able to safely access payload.
(c) for v = 11, fetch_add would have parttook in synchronization with x.store(1) for its read operation followed by synchronizing with x.load() after its write part so payload = 7 ran before x.store(1) in A, then in B x.fetch_add(10) ran and that synced with x.load() in R. so in this case - yes - R can safely access payload.
(d) for v = 5, this means that x.store(5) in C ran and that synced with the read in R, i.e., x.load(). so - no - in this case, R cannot safely access payload.
(e) for v = 15, to keep it short - this means that after instantiation of x, thread B ran and thread C ran and then R ran, and in this case - no - R cannot access payload safely.

3. (Recall) 5.3.1 says synchronizes-with can come from only one kind of operation. Which kind? Locking and unlocking a `std::mutex` also orders operations between threads: where does the book say that ordering comes from?

Answer: the book states that operations on a mutex protected data structure can have synchronizes with type of ordering between its operations; however, this might only happen if internal data members of the structure include atomic types and the structure relies on atomic operations. So essentially - the synchronization between operations comes from using atomic types properly in the methods of a data structure.

4. (Code-tracing) Single thread.

```cpp
int next_id() { static int i = 0; return ++i; }
void send(int a, int b) { std::cout << a << ',' << b << '\n'; }
int twice(int v) { return 2 * v; }
```

   For each pair, is the first call sequenced before the second (yes / no / "unordered")?
   (a) The two `next_id()` calls in `send(next_id(), next_id());`
   (b) The two `next_id()` calls in `int k = (next_id(), next_id());` (built-in comma operator)
   (c) The `next_id()` call and the body of `twice` in `twice(next_id());`
   (d) The two `next_id()` calls in `send(next_id(), 0); send(next_id(), 0);` (two statements on one line)

   If (a) is the first line the program runs, what can it print? Is that behaviour undefined or unspecified, going by the book?

Answer:

(a) the calls are not sequenced, the order is unspecified or Indeterminately sequenced.
(b) this doesnt look like valid syntax to me, i.e., how are we evaluating the calls once they are done? `int k = (next_id(), next_id());` doesnt seem sensible to me.
(c) `twice(next_id());` will be sequential since the function `next_id()` will be evaluated and its value will be supplied into the twice() function and even if it accepted functions which it doesnt, the body is still 2*v which would still mean a single and hence ordered execution.
(d) two statements on one line seperated by `;` which means sequentially ordered statements, one after the other or ordered execution.

if (a) is the first line then it can print (1, 2) or (2, 1). the behavior is called unspecified and not undefined as the standard leaves it upto the compiler to handle the order of execution.

Confidence-tag each answer (low/med/high).

### Beyond the book (optional, ungraded; not queued unless you say "queue it")

5.3.1's case (b) ("a subsequent atomic write on x by the same thread that performed W") is the C++11/14/17 *release sequence* rule. **C++20 removed it** (paper P0982R1, "Weaken release sequences"): now only the read-modify-write chain continues W's release sequence. A later plain `store` by W's thread no longer counts as part of it. With the default ordering this makes no practical difference: the later store is itself a suitably tagged write, so a load that reads it synchronizes-with that store directly, and everything before W is sequenced before it anyway. It only matters once the later store is weaker (e.g. `memory_order_relaxed`, from 5.3.3). Grade Q2 by the book's rule.

Also: Listing 5.2 as printed won't compile. The wait is `std::this_thread::sleep_for(std::chrono::milliseconds(1))`, and it needs `<thread>` and `<chrono>`.

## Review: 5.3 intro, 5.3.1–5.3.2 (2026-09-30)

No confidence tags on any answer this time. Please add them next worksheet: they're how confident misses get found, and Q1(b)/(c) are exactly that kind.

### Warm-up

- **2.5-thread-id-ordered-vs-hash -> hit.** > "the ability of std::thread::id to have a complete set of comparison operators allows for objects of this type to be used as keys in associative containers like std::map or be sorted … std::hash<std::thread::id> which allows for the use of std::thread::id as keys in non-associative containers". The two are now separate facilities (the original miss blended them), and "sorted" answers the total-order part. Nits: `std::unordered_map` is an *unordered associative* container, not a "non-associative" one. You didn't say the "never touches" half explicitly: `std::map` never hashes, and `std::unordered_map` never calls `<`.
- **4.3-timeout-return-types -> hit.** > "`fut.wait_for(50ms)` will simply return `std::future_status::deferred` rather than waiting for the acknowledgement or starting the function, so the loop repeatedly takes the `else` branch". All three types and their values are right. Stated plainly: the loop never ends, because `fetch_ack` only runs when `get()` (or `wait()`) is called, and the loop only calls `get()` after `ready`.
- **5.2-cas-expected-overwrite -> hit.** > "the CAS sets `expected = true` … this second time though the expected is true and busy is true and now even though the R1 is not done with rebalance(), R2 shows up and runs it too". The trace is correct, and the reset-inside-the-loop version is a correct `claim()`. Missing: `try_claim()`. It's the same call without the loop: `bool expected = false; return busy.compare_exchange_strong(expected, true);`. Graded hit because the probed point (a failed CAS overwrites `expected`) came through in both the trace and the fix.

### Quiz

**Q1 -> miss** (probed point: synchronizes-with exists only when the load reads the stored value, and it only orders what comes *after* the load)

(a) > "(1) is sequenced before (2) … (2) synchronizes with (3) … (3) sequenced before (4) … happens before is transitive which means … (1) happens before (4)." Correct, and it's the book's own argument for Listing 5.2. Precision: the 5.3.2 rules are that (2) synchronizes-with (3), so (2) *inter-thread happens-before* (3). Then "A sequenced before B, B inter-thread happens-before C" gives (1) ITHB (3), and "A ITHB B, B sequenced before C" extends it to (4). The book states both combinations.

(b) > "there isnt any issue of operation reordering happening since we have default seq_cst memory order with the (3) load which avoids the size read or book[0] access operations beeing reordered above (3) … the problem i see is the missing while loop". **Wrong.** When (3) reads `false`, there's no synchronizes-with at all. The rule in 5.3.1 needs the load to *read the value stored* by the write. So nothing orders (4) `book.size()` against (1) `push_back`. Two non-atomic accesses to the same data, one of them a write, with no enforced ordering: the book calls that undefined behaviour (Listing 5.2 discussion). `seq_cst` on the load doesn't help. Ordering between threads comes from happens-before relations, and a load that read `false` hasn't formed one. Your loop *is* a valid fix, but for this reason: it guarantees no line after the loop runs until a load has read `true`. The problem isn't that "(4) might not run". (Your snippet still uses `r`, which no longer exists.)

(c) > "I want to say yes for this instance since the line (4) is still sequence before its check in (5)". **Wrong: no.** After the swap, (4) is sequenced *before* (3). The synchronizes-with from (2) to (3) makes (1) happen-before (3) and everything sequenced after (3). It says nothing about operations *before* (3). Nothing makes (1) happen-before (4), so `book.size()` can run during `push_back`. That's a data race (UB) even in runs where `r` is `true`. Reading `true` later doesn't reach back in time. Only (5)'s `book[0]` is safe.

The same wrong model shows up in both (b) and (c): the atomic is an "anchor" that stops reordering around it. The book's model is different. An atomic load contributes ordering only through the happens-before chain it's part of: it has to read the stored value, and it only orders what's sequenced after it. This is also the other half of `5.1-data-race-definition` (atomics don't "serialize" plain accesses). Queued as a misconception.

**Q2 -> hit** (all five verdicts correct)

- v=1 > "x.load() synced with x.store(1)": yes, case 1 (reads W itself).
- v=2 > "x.load() was bound to x.store(2) and payload = 7 and x.store(1) happened before that thanks to sequenced before": yes. The book's case is 2: a later atomic write by W's own thread. Your reasoning (store(2) synchronizes directly, and `payload = 7` is sequenced before it) also holds with the default ordering. See Beyond the book: that's exactly the C++20 view.
- v=11 > "fetch_add would have parttook in synchronization with x.store(1) for its read operation followed by synchronizing with x.load()": yes. The book's case is 3: a read-modify-write sequence whose first RMW read W's value (B read 1).
- v=5 > "x.store(5) in C ran and that synced with the read in R … no": correct. C is a different thread and a plain store, so neither case 2 nor case 3 fits.
- v=15 > "after instantiation of x, thread B ran and thread C ran and then R ran … no": the verdict is right, **but the order is backwards.** B then C would leave 5 (C's store overwrites). 15 means C stored 5 *first*, then B's `fetch_add` read 5. The RMW chain started from C's value, not W's, so case 3 doesn't apply. That's the one subtle case in the question, and it's the reason core item `5.3-sync-with-three-cases` exists.

**Q3 -> miss** (probed point: synchronizes-with comes only from operations on atomic types; a mutex provides ordering because its lock/unlock perform atomic operations internally)

> "operations on a mutex protected data structure can have synchronizes with type of ordering between its operations; however, this might only happen if internal data members of the structure include atomic types". The first half is right: the ordering comes from atomic operations. But the answer applies it to the **mutex-protected structure**, not to the **mutex**. In the book's sentence, "a data structure (such as locking a mutex)", the mutex itself is the data structure. A `std::mutex` gives you ordering because `lock()`/`unlock()` perform atomic operations inside the mutex. A structure protected by a mutex (your `threadsafe_stack`, `threadsafe_queue`) needs **no** atomic members: its plain data becomes visible to the next thread that locks, through the mutex's own synchronizes-with. Read literally, the answer implies ch3/ch4's builds would need atomics to be correct. They don't.

**Q4 -> hit** (a, c, d and the output part correct; b wrong)

- (a) > "the calls are not sequenced, the order is unspecified": correct.
- (b) > "this doesnt look like valid syntax to me": **wrong.** It's valid. Inside parentheses, `,` is the built-in comma operator: it evaluates the left operand, discards it, then evaluates the right operand, and its value is the right one. It's one of the two exceptions the book names: the left call is sequenced before the right, and `k` gets the second id. (Commas that *separate function arguments* aren't this operator. That's why (a) is unordered.)
- (c) > "will be sequential since the function `next_id()` will be evaluated and its value will be supplied into the twice()": correct. The other exception (a result used as an argument).
- (d) correct: separate statements are always sequenced.
- Output > "(1, 2) or (2, 1). the behavior is called unspecified and not undefined": correct.

Graded hit because 4 of 5 parts are right, including the probed rule (arguments are unordered, and that's unspecified, not UB). The comma-operator exception goes into core item `5.3-single-statement-sequencing`.

### Build

None. Triage deferred the build decision to 5.3.3 (the flag-publish pattern becomes worth building once the ordering tags exist).

### Queue updates

```
python3 tools/review.py log 2.5-thread-id-ordered-vs-hash hit --note "5.3.1-5.3.2 warm-up: comparison ops -> std::map/sorting, std::hash -> unordered_map; no longer blended. Called unordered_map 'non-associative' (it's unordered associative)"
python3 tools/review.py log 4.3-timeout-return-types hit --note "5.3.1-5.3.2 warm-up: cv_status / bool / future_status{ready,timeout,deferred}; deferred async: wait_for returns deferred, loop logs forever, task runs only on get()"
python3 tools/review.py log 5.2-cas-expected-overwrite hit --note "5.3.1-5.3.2 warm-up: traced fail->expected=true->true==true succeeds, R2 runs concurrently; reset-expected claim loop correct. Didn't write try_claim()"
python3 tools/review.py add 5.3 5.3-sync-needs-value-read "Flag publish: a load that reads false, or a plain read placed BEFORE the load that reads true -- is either ordered after the writer's plain writes? Which relation is missing and what's the consequence?" --kind misconception --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(b)(c)"
python3 tools/review.py add 5.3 5.3-mutex-sync-internal-atomics "Synchronizes-with comes only from which operations? Where does std::mutex's cross-thread ordering come from, and does a mutex-protected struct need atomic members?" --kind miss --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q3"
python3 tools/review.py add 5.3 5.3-sync-with-three-cases "Which writes can a load read from and still synchronize-with store W on x (the three cases)? Why don't another thread's plain store, or an RMW chain that started from someone else's value, count?" --kind core --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q2"
python3 tools/review.py add 5.3 5.3-single-statement-sequencing "Sequencing within one statement: function-argument evaluation order (unspecified vs UB), the two exceptions (built-in comma operator, nested call as argument), and statement-to-statement." --kind core --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q4"
python3 tools/review.py add 5.3 5.3-ithb-combination-rules "Inter-thread happens-before: how it arises from synchronizes-with, its transitivity, and the two sequenced-before combinations -- and why one synchronizes-with suffices for a whole batch of writes." --kind core --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(a)"
```

Q2 and Q4 were hits, but their subtle sub-cases (v=15; the comma operator) are covered by the core items above rather than by miss items.

### Fix list

None opened or closed. F1–F10 are still open.

### Beyond the book (ungraded)

- The shape (one writer overwriting a small struct, readers that want a consistent copy without a lock) is what a **seqlock** solves: a version counter that is odd during a write, and readers that retry when it changed or was odd. Doing it correctly needs the ordering tags from 5.3.3 plus fences (5.3.5), and the data fields themselves have to be atomics (relaxed) to avoid a data race. Worth revisiting after 5.3.5.

## 5.3.3 — Memory ordering for atomic operations

```
$ python3 tools/review.py due   (2026-09-30, after the 5.3.1–5.3.2 commit)
Due now: 38 active (29 overdue) | maintenance due: 0 | due in next 3 days: 25 | total active: 71, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 4.1-notify-outside-lock  (sec 4.1, 1d overdue, streak 0)
  2. [misconception] 5.1-data-race-definition  (sec 5.1, due today, streak 0)
  3. [miss] 5.2-cas-failure-ordering  (sec 5.2, 4d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.3 Q3
Also due (next worksheets): 35 more (5.2-cas-update-loop, 5.2-memory-order-by-operation, 3.3-map-concurrent-write-unsafe, ...)
Open fix-list items: 10 (F1-F10)
Lint: OK
```

Queue note: the oldest items are 5 days overdue (`3.2-deadlock-both-locks-held` since 2026-09-25). They pass the framework's 7-day review-only trigger on 2026-10-02. After this worksheet, the next one should be review-only.

Triage:
- Seq_cst's single total order vs. acquire-release's pairwise sync (Listings 5.4 and 5.7), and "seq_cst on *every* thread or you lose the benefit": **code-tracing** (Q1).
- Acquire and release must be **paired** and the acquire must **read the released value** (Listing 5.8 and its "if it weren't a while loop" / "if either were relaxed" variants): **code-tracing** (Q2). This is also the direct probe of the `5.3-sync-needs-value-read` model.
- Relaxed ordering: the two mental models the book says to throw out, the one remaining requirement (per-variable modification order), and which Listing 5.6-style outputs are legal: **recall + code-tracing** (Q3).
- Read-modify-write orders: an acquire-only RMW isn't a release, and a release-only RMW's read isn't an acquire; seq_cst RMWs count as both: **code-tracing** (Q4).
- Mutex lock = acquire, unlock = release; "same mutex / same variable"; why a lock built from acquire/release looks sequentially consistent to its users: **recall** (Q5). This follows up the 5.3.1–5.3.2 Q3 miss.
- **Build: `publish_once`**. This is the build deferred from 5.3.1–5.3.2. The release/acquire publish (Listings 5.2 and 5.8) plus the transitive/claim pattern (Listing 5.9's CAS) is a reusable mechanism, and it's the kind you verify by breaking: weaken one order and watch TSAN report it. Checked in scratch today: a relaxed store or a relaxed load on the flag gets flagged 10/10 under TSAN, and release/acquire is clean 10/10. One sitting. Say so if you'd rather skip it.
- `memory_order_consume` is named in the list of six but explained in the next part of 5.3.3 (data dependency), which isn't in this paste. Not quizzed. Listing 5.9's transitive chain is part of Q4's setup and the build; it isn't quizzed separately.

### Warm-up

1. An order-router consumer runs `std::unique_lock lk(m); cv.wait(lk, [&]{ return !orders.empty(); });`. The producer runs `{ std::lock_guard g(m); orders.push(o); } cv.notify_one();`, so the notify happens after the lock is released. Take two timings: (i) the producer's push and notify both finish before the consumer ever reaches `wait`; (ii) the consumer is already asleep inside `wait` when the producer runs. For each, walk through what `wait(lk, pred)` does step by step: when it evaluates the predicate, and whether `m` is held at that moment. Can the consumer end up asleep while an order sits in the queue?

Answer:

(i) If `notify_one()` runs after the lock is released and the consumer has not yet reached `wait()`, the notification can be missed. However, when `wait(lk, pred)` is first called, it evaluates the predicate while holding the lock. If the producer has already pushed an order, `!orders.empty()` is true, so `wait()` returns immediately without putting the consumer to sleep. Therefore, the missed notification does not matter because the predicate checks the actual state of the queue.

(ii) In the second case, the consumer is already waiting. The predicate was initially false, so `wait()` atomically releases the lock and puts the consumer to sleep. The producer then acquires the lock, pushes the order, releases the lock, and calls `notify_one()`. The consumer wakes, reacquires the lock, and checks the predicate again. If `!orders.empty()` is true, `wait()` returns while keeping the lock held; if it is false, the consumer goes back to sleep and releases the lock again.

The consumer cannot end up asleep simply because it missed a notification, as long as the queue is modified while holding the same mutex and the predicate correctly represents the condition being waited for. Even if multiple orders were added before the consumer started, the first predicate check would see that the queue is non-empty and return immediately. The condition variable does not keep track of missed notifications; the predicate checks the current state instead.


2. Two feed-handler threads both run `last_px.store(p, std::memory_order_relaxed);` on the same `std::atomic<std::int64_t> last_px` at the same instant. 5.3.3 says relaxed operations don't synchronize-with anything and impose almost no cross-thread ordering. Give the Standard's definition of a data race, with every condition. Is this a data race? Name the condition that decides it.

Answer: the standards definition of a data race is - two or more threads simultaneously access the same memory location, at least on of them is a write operation, and there is no enforced order between the operations.

the question is right because it is tricky, since both are write operations, however, since we have an atomic variable, even in relaxed memory order, even if there is simultaneous request to do an operation on the atomic variable, it is my understanding that the compiler decides to put one in front of the other, since atomic operations are - as the name implies - atomic, wether by instruction or atomic lock. another model that comes to mind is that both threads update the copies of the atomic var in their L1 caches and then the one which is more recent, prevails in cache coherence operation which is done on a priority basis before next operations on this variable - read or write. these are the two scenarios that come to mind - i am however confident that this is not a data race since atomicity and modification order respect is guaranteed - even in the case of relaxed memory ordering.

3. A retry loop calls `px.compare_exchange_weak(old, next, std::memory_order_acq_rel)`, giving only one ordering.
   (a) Which ordering applies when an attempt fails, and why that one?
   (b) Same question when the single ordering given is `memory_order_release`, and when it's `memory_order_seq_cst`.
   (c) Someone writes `px.compare_exchange_weak(old, next, std::memory_order_acquire, std::memory_order_release)`. Which values does the book forbid as the failure ordering, and why?
   (d) What is the book's *other* restriction relating the failure ordering to the success ordering?

Answer: 
(a) if the write part fails the the CAS operation cant be classified as a release operation on the down swing and so it's failure memory order has to be demoted to `std::memory_order_acquire` since that was the part that succeeded.

(b) if the success memory order was given `memory_order_release` then the failure case memory order will be set to `std::memory_order_relaxed`. If the success memory order was given `memory_order_seq_cst` the the failure case memory order is set to be `std::memory_order_acquire`.

(c) the book forbids using `memory_order_release`, `memory_order_seq_cst`, `memory_order_acq_rel` for failure case in the described scenario as these are meant to synchronize read operations with appropriate memory order on the down swing and it wouldn't make sense if the read didn't succeed.

(d) I believe the general rule is that the failure case ordering for `memory_order_seq_cst` is also set to be `memory_order_seq_cst` which is sort of an exception to the general paradigm of the failure ordering to be read synchronization oriented only.

### Quiz

1. (Code-tracing) Two risk monitors watch two venues' halt flags. Venue A's handler sets `halt_a`, and venue B's handler (a different thread) sets `halt_b`, each exactly once.

```cpp
std::atomic<bool> halt_a{false}, halt_b{false};

void venue_a()   { halt_a.store(true, ST); }
void venue_b()   { halt_b.store(true, ST); }

void monitor_1() { 
    while (!halt_a.load(LD)); 
    bool b_seen = halt_b.load(LD); 
    /* log "A first" if !b_seen */ 
    }
void monitor_2() { 
    while (!halt_b.load(LD)); 
    bool a_seen = halt_a.load(LD); 
    /* log "B first" if !a_seen */ 
    }
```

   (a) With `ST = LD = memory_order_seq_cst`: can monitor_1 log "A first" while monitor_2 logs "B first" in the same run? Why or why not? Use the book's argument.
   (b) With `ST = memory_order_release` and `LD = memory_order_acquire`: same question. What exactly does each release/acquire pair order, and what doesn't it order?
   (c) Back to (a), but monitor_2 is changed to use `memory_order_relaxed` for its loads, and everything else stays seq_cst. Does (a)'s guarantee still hold? What does the book say about this?

Answer (med):

(a) No. With seq_cst, all four atomic operations participate in one single total order consistent with each thread's program order. If monitor 1 sees halt_a == true and then sees halt_b == false, the total order requires A's store to precede B's store. Therefore monitor 2 cannot subsequently observe halt_b == true while observing halt_a == false. Both monitors cannot report their respective venue as first in the same execution.

(b) With `ST = memory_order_release` and `LD = memory_order_acquire`, it is possible for `"A first"` to print while `"B first"` does not. It is also possible for neither to print, and unlike the `seq_cst` case, it is possible for both to print.

Consider this execution:

```cpp
Thread 1: venue_a
    halt_a.store(true, release)          // S_A
                ↓
Thread 3: monitor_1
    halt_a.load(acquire) → true          // L_A
                ↓
    halt_b.load(acquire) → false         // L_B
    print "A first"

Thread 2: venue_b
    halt_b.store(true, release)          // S_B
                ↓
Thread 4: monitor_2
    halt_b.load(acquire) → true          // L_B2
                ↓
    halt_a.load(acquire) → true          // L_A2
    does not print "B first"
```

The important rule is:

 **An acquire load synchronizes-with a release store only when the acquire load observes the value written by that release store.**

Therefore, `S_A` and `L_A` synchronize:

```cpp
S_A
 ↓ synchronizes-with
L_A
 ↓ sequenced-before
L_B
```

But `L_B` reads `false`. It therefore did **not** observe `S_B`, which stores `true`. Consequently, there is **no synchronizes-with relationship between `L_B` and `S_B`**.

`S_B` can then execute later:

```cpp
L_B → false

        ...

S_B → true
 ↓
L_B2 → true
```

Here `S_B` and `L_B2` do synchronize because `L_B2` actually observes the `true` written by `S_B`.

So monitor 1 can print `"A first"`, while monitor 2 sees both flags as `true` and does not print `"B first"`.

It is also possible for `venue_b` to execute before monitor 1's `halt_b.load()`, in which case monitor 1 sees `true` and `"A first"` does not print. Similarly, there can be an execution where neither monitor prints.

The key difference from `seq_cst` is that release/acquire only gives synchronization for the **specific release/acquire pair that actually communicates through that atomic variable**. It does not impose one single global ordering across `halt_a` and `halt_b`.


(c) The guarantee from (a) no longer holds because `monitor_2()` now uses `memory_order_relaxed` for its loads. Therefore, all three outcomes are possible, including **both `"A first"` and `"B first"` printing in the same run**.

**Nothing prints:** Suppose both stores happen before their respective monitor checks:

```cpp
venue_a:    halt_a.store(true, seq_cst)
venue_b:    halt_b.store(true, seq_cst)

monitor_1:  halt_a.load(seq_cst) → true
            halt_b.load(seq_cst) → true
            → no "A first"

monitor_2:  halt_b.load(relaxed) → true
            halt_a.load(relaxed) → true
            → no "B first"
```

**"A first" prints:** Suppose `monitor_1()` observes `halt_a == true` while `halt_b` is still false:

```cpp
venue_a:    halt_a.store(true, seq_cst)

monitor_1:  halt_a.load(seq_cst) → true
            halt_b.load(seq_cst) → false
            → "A first"

venue_b:    halt_b.store(true, seq_cst)

monitor_2:  halt_b.load(relaxed) → true
            halt_a.load(relaxed) → true
            → no "B first"
```

**"B first" prints:** Symmetrically:

```cpp
venue_b:    halt_b.store(true, seq_cst)

monitor_2:  halt_b.load(relaxed) → true
            halt_a.load(relaxed) → false
            → "B first"

venue_a:    halt_a.store(true, seq_cst)

monitor_1:  halt_a.load(seq_cst) → true
            halt_b.load(seq_cst) → true
            → no "A first"
```

**Both print:** This is the important new possibility. `monitor_1()` can see `halt_a == true` and `halt_b == false`, while `monitor_2()` uses relaxed loads and can see `halt_b == true` and `halt_a == false`:

```cpp
venue_a:    halt_a.store(true, seq_cst)

monitor_1:  halt_a.load(seq_cst) → true
            halt_b.load(seq_cst) → false
            → "A first"

venue_b:    halt_b.store(true, seq_cst)

monitor_2:  halt_b.load(relaxed) → true
            halt_a.load(relaxed) → false
            → "B first"
```

The reason this was impossible in (a) is that **all the relevant operations were `seq_cst` and therefore participated in one single total order**. Here, `monitor_2()`'s relaxed loads do not participate in that `seq_cst` total order, so the guarantee from (a) is lost.


2. (Code-tracing) `px` is a `std::atomic<int>` starting at 0. `ready` is a `std::atomic<bool>` starting at false.

```cpp
// Writer
px.store(101, std::memory_order_relaxed);
ready.store(true, W);

// Reader, version L (loop):  
while (!ready.load(R)); 
int p = px.load(std::memory_order_relaxed);

// Reader, version I (if):    
bool r = ready.load(R);  
int p = px.load(std::memory_order_relaxed);
```

   For each case, what values can `p` hold, and why?
   (a) `W = release`, `R = acquire`, version L.
   (b) `W = relaxed`, `R = acquire`, version L.
   (c) `W = release`, `R = relaxed`, version L.
   (d) `W = release`, `R = acquire`, version I, in a run where `r` is `false`. And in a run where `r` is `true`?
   (e) In case (b), suppose `px` were a plain `int` instead of an atomic (and the reader read it plainly). What changes, going by the book?

Answer:
(a) for this case the while loop will keep checking `ready` and when the `ready.store()` releases the loop by being observed as `true`, the acquire load synchronizes with the release store. The `px.store(101)` is sequenced-before the `ready.store()` in the writer, and the `px.load()` is sequenced-after the `ready.load()` in the reader, so we get a happens-before chain from the `px.store()` to the `px.load()`. Therefore `p` will contain **101**.

(b) in this case the writer's `ready.store()` is relaxed while the reader's load is acquire, so there is no synchronizes-with relationship between them. The loop will nevertheless keep checking until it observes `ready == true`, but observing `true` does not establish the release/acquire synchronization because the store that wrote `true` was relaxed. Therefore there is no happens-before relationship from the `px.store(101)` to the `px.load()`. Since `px` is still atomic, the load is valid and `p` can be **0 or 101**.

(c) here the writer uses a release store but the reader uses a relaxed load. Again, there is no synchronizes-with relationship because the acquire operation is missing. The loop will still keep checking until it observes `ready == true`, but the relaxed load does not establish the ordering guarantee provided by the release store. Therefore the `px.store(101)` does not happen-before the `px.load()`, and since `px` is atomic, `p` can again be **0 or 101**. The release store by itself does not force another thread to observe the preceding `px.store()`; it needs a corresponding acquire operation that observes the release.

(d) in this case we have release/acquire on `ready`, but the reader uses a single `if` rather than a loop. If `r` is **false**, then the acquire load did not observe the writer's release store, so there is no synchronizes-with relationship and `p` can be **0 or 101**. If `r` is **true**, then the acquire load can synchronize with the release store that wrote `true`. We then have `px.store(101)` sequenced-before the release store, the release store synchronizes-with the acquire load, and the acquire load is sequenced-before the `px.load()`. Therefore `px.store(101)` happens-before the `px.load()`, so `p` must be **101**. The important point is that the acquire load itself does not block; it simply establishes synchronization if it observes the value written by the release store.

(e) in case (b), if `px` were instead a plain `int`, then the writer would perform a non-atomic write to `px` while the reader performs a non-atomic read, and there is still no happens-before relationship between them because `ready` uses relaxed ordering. Therefore the two conflicting accesses constitute a **data race**, which gives **undefined behavior**. This is different from atomic `px`, where the accesses themselves are atomic and the possible result can be 0 or 101.

3. (Recall + code-tracing)
   (a) The book says that once you leave sequential consistency you must throw out *two* mental models. Which two? With no other ordering constraints, what is the *only* thing all threads must agree on?
   (b) Listing 5.6's program, but with `loop_count = 4`: three incrementer threads (one each for x, y, z; iteration `i` reads x, y, z and then stores `i+1` to its own variable) and reader threads, all relaxed. Each line is one thread's four `(x,y,z)` triplets. Which of these are possible? For each impossible one, name the rule it breaks.
      - A. x-incrementer: `(0,0,0),(1,2,0),(2,1,0),(3,3,3)`
      - B. y-incrementer: `(0,0,0),(0,1,0),(0,2,0),(0,3,0)`
      - C. z-incrementer: `(0,0,0),(0,0,1),(4,4,1),(4,4,3)`
      - D. From the same run, reader 1: `(0,0,0),(2,0,0),(2,1,0),(4,4,4)` and reader 2: `(0,0,0),(0,2,0),(1,2,0),(4,4,4)`

Answer:

(a) there are two points the author emphasized on:
- threads don't have to agree on order of events.
- in the absence of ordering constraints, threads only need to agree on modification of the atomic variables.
so what i gathered from this is that when we move out of sequentially consistent ordering, if one thread does update an atomic variable x, that doesn't mean another thread will see that updated immediately in its own read of the atomic variable. this however doesnt mean that the other threads will indefinitely keep sitting with stale values and it definitely doesnt mean that they will go backwards to older values, it means that cache coherence will catch them up eventually however there is no constraint on when they get and read the most recent value or change, more over, agreeing on the modification order means that if the order of writes to atomic<int> x have been 1, 2, 3, 4, then thread A might have caused the changes, thread B in its reads might have seen 1,2,3,3,3,4 and C might have seen 2,3,4,4 but never does any of them read 4,3 or 3,2, so this is what it means to agree on modification order and underneath it is a reliance on the cache coherence hardware phenomenon to which different memory orders adhere to differently based on the guarantees they need to uphold.

(b) A, B and D are possible since memory order is being respected and the updates to relevant properties of the struct in A and B tracks, i.e., x updates in A and y updates in B are looking sequential and obedient to the way the threads are running their respective functions. However if we look at the output of z-incrementer, we see  `(0,0,0),(0,0,1),(4,4,1),(4,4,3)` and we see a missing 2 at the third iteration which ought to have been 2. this doesnt track since the caller explicitly updates z in this thread for each iteration so this isnt possible.

4. (Code-tracing) Read-modify-write orderings. Thread P publishes, and thread M then moves `flag` from 1 to 2 with one RMW whose ordering is `ORD`:

```cpp
int payload = 0;                    // plain; written only by P
int note = 0;                       // plain; written only by M
std::atomic<int> flag{0};

void P() { 
    payload = 42; 
    flag.store(1, std::memory_order_release);
    }

void M() {
    note = 7;                                                  
    // M's own write, BEFORE its RMW
    int e = 1;
    // waits for P, then 1 -> 2
    while (!flag.compare_exchange_strong(e, 2, ORD)){
        e = 1;   
    } 
    int seen = payload;                                        
    // M reads P's write AFTER its RMW
}

void Q() { 
    while (flag.load(std::memory_order_acquire) < 2); 
    int n = note; 
    }
```

   For `ORD` = `relaxed`, `acquire`, `release`, `acq_rel`, and `seq_cst`:
   (i) Is M's read of `payload` guaranteed to see 42 (and not a data race)?
   (ii) Is Q's read of `note` guaranteed to see 7 (and not a data race)?
   Use the book's two sentences about `fetch_sub` with acquire and `fetch_or` with release, and its rule for how seq_cst operations behave when mixed with acquire-release.

Answer:
here is answers of (i) and (ii) for the different ORDs or memory_orders:
- `relaxed` > although there is sequenced before and thus happens before relation between the writes to payload and its flag store and flag load and n note, intuitively the while loop blocks ought to be enough for giving us guaranteed reads off payload and note, however thats not what actually transpires. the issue is that relaxed ordering makes no guarantee even for the surrounding atomic variables for immediate observability of their changes - only that the modification order will be respected by other threads, the modification order guarantee isnt for normal shared variables though. so the payload = 42 write might be going on while seen tries to read from it in thread M and similarly, note = 7 might not have yet been reflected in its cache memory of thread Q before its read and so we have no guarantee for either.
- `acquire` > setting acquire for ORD will allow CAS to sync with flag.store, so the writes above it should all be sealed before they are viewed below, i.e., for (i) we ought to have a guaranteed read of 42. (ii) there is still no sync with the while loop load.(acquire) in Q() and so there may be data race for reading note.
- `release` > in this release setting for ORD, we get no sync between flag.store() operation in P() and so we might get race condition on read of payload since cross thread sync between operations in P() and M() isnt established. However, release as ORD on flag does sync with acquire on flag.load() in Q() and so that means the operations above CAS in M(), i.e., note = 7 and e = 1 are sealed the the bound thread's acquire, or, the release operation synchronizes with an acquire operation that observes its value, establishing happens-before for the preceding operations. so we get a guaranteed read of note in Q(). So (i) not guaranteed read of payload (ii) guaranteed read of note.
- `acq-rel` > now the read part of this CAS in M() has acquire from acq-rel and write part has release from acq-rel. this means that the CAS in M() can bind with the store in P() and load in Q(). therefore we are guaranteed safe reads of both payload and note. (i) payload's safe read guaranteed, (ii) note's safe read guaranteed.

- `seq_cst` > seq_cst has acquire semantics and release semantics: the acquire side orders operations after the CAS, while the release side orders operations before the CAS. this means that the CAS in M() can bind with the store in P() and load in Q(). therefore we are guaranteed safe reads of both payload and note. (i) payload's safe read guaranteed, (ii) note's safe read guaranteed.

5. (Recall) In acquire-release terms, what are `mutex::lock()` and `mutex::unlock()`? What's the mutex version of "acquire and release have to be on the same variable"? A spinlock built from acquire/release operations isn't sequentially consistent internally: why does code *using* it still see sequentially consistent behaviour?

Answer (med): an acquire release terms:
`mutex::lock()` is `std::memory_order_acquire`
`mutex::unlock()` is `std::memory_order_release`
in my experience -  the mutex version of "acquire and release have to be on the same variable" is the same mutex needs to be used for producer and consumer functions.
though mutexes built from acquire/release operations isnt sequentially consistent, i.e., doesnt used seq_cst, it still demonstrates sequentially consistent behavior because it gives - the thread holding the lock - exclusive access, which serializes thread access to the protected critical section on a global level.

Confidence-tag each answer (low/med/high). Last worksheet had none.

### Build task: `publish_once`

**What it is.** A slot that gets a value exactly once, from whichever publisher gets there first, and is then read by any number of threads without a lock. Example use: per-venue session metadata (tick size, lot size, fee tier). Several connector threads may fetch it at startup; the first to arrive publishes it, and strategy threads read it on every quote afterwards.

**Folder** (scaffolded, files empty): `ch05_the_cpp_memory_model_and_atomic_operations/publish_once/`, with `include/publish_once.hpp` and `tests/publish_once_driver.cpp`. It's added to the root `CMakeLists.txt`.

**Interface:**

```cpp
template <typename T>
class publish_once {
public:
    publish_once();                                   // starts empty
    publish_once(const publish_once&) = delete;
    publish_once& operator=(const publish_once&) = delete;

    bool publish(T value);     // the first call wins: it stores the value and returns true.
                               // Every other call returns false right away and changes nothing,
                               // even if the winner hasn't finished storing yet.
    const T* try_get() const;  // nullptr until the value is fully published; afterwards,
                               // a pointer to it (the same address on every call, from every thread)
    const T& wait() const;     // spins (with std::this_thread::yield()) until published, then
                               // returns a reference to the value
};
```

**Thread-safety contract:**
- Any number of threads may call any member at the same time.
- Exactly one `publish()` call ever returns true. The value never changes after that.
- A reader that gets a non-null pointer from `try_get()`, or returns from `wait()`, sees the *whole* value exactly as the winner stored it. It sees no half-written fields and no default/garbage state.
- A reader never touches the value while the winner is still writing it.
- The value itself is stored as plain (non-atomic) memory. The ordering comes from your atomic(s), not from making `T` atomic. `T` only needs to be move- or copy-constructible; don't require a default constructor.

**Memory-order rules for this build (this is the 5.3.3 retrieval):**
- Every atomic operation names its ordering explicitly. No defaulted `seq_cst` anywhere.
- Use the weakest ordering that is correct for each operation, with a one-line comment on each saying why it's enough. Where you use `relaxed`, the comment should say why nothing depends on it for synchronization.

**Tests to write:**
1. **One publisher, many readers, all fields visible.** 8 reader threads call `wait()` (half of them could poll `try_get()` instead), and 1 publisher publishes a struct holding an array of 1,000 `int`s all set to the same nonzero value `k`, plus a separate `int` field `2*k`. Start all threads together behind a start flag, and repeat the whole thing for ~200 fresh `publish_once` objects (a new `k` each round). **Assert:** every reader sees every array element equal to `k`, and the extra field equal to `2*k`. **Catches:** a missing or misplaced release/acquire, where a reader sees the flag before the data. On x86 the assert itself may never fire; TSAN under `verify.sh` is what reports it. That's expected, and it's why the gate exists.
2. **Racing publishers.** 8 threads each call `publish(i)` (distinct values) at the same time, released by a start flag, and ~200 rounds as above. **Assert:** exactly one call returns true per round, and a reader that `wait()`s afterwards gets that winner's value. **Catches:** a check-then-act `publish()` (load the state, then store) that lets two publishers both "win", and one overwrite the other's value while readers read it.
3. **Empty, then stable.** On a fresh object, `try_get()` returns `nullptr`. After a publish, `try_get()` is non-null, and every reader's `try_get()`/`&wait()` gives the **same address**. A second `publish()` returns false and leaves the value unchanged. **Catches:** returning a copy, or re-storing on a later publish.

**Before review, run:** `tools/verify.sh publish_once`. It should print `TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK`. For a quick check that your tests have teeth, copy the header to scratch, weaken your release to `relaxed`, and confirm TSAN reports it. (Don't commit the weakened copy.)

**Self-explanation (2–4 sentences, closed-book, write it before review):** For each atomic operation in your class, say why the ordering you chose is enough. Then: what can a reader observe if the store that marks "published" were `memory_order_relaxed`? Why might the assert in Test 1 still pass on x86 while TSAN reports it? And why must a reader that observes "a publisher is writing right now" not read the value?

Self-explanation: Ok so for the internal member - a lone pointer was the obvious choice given the many constraints with directly making type T as atomic which we cannot predict nor control. furthermore, in the publish, i used memory_order_seq_cst because with the publish, i pictured that there might be reader threads racing right behind or  right besides and i wanted to guarantee that all other threads must be able to see the publish right before they do their operations, be they reads or trying to publish and I also feared that if i had just used acq_rel then the other threads interleaved beside this one for publishing might not register the publishing because unlike mutex, acq_rel CAS isnt exclusive access and so seq_cst felt like a safe choice for this operation. for load in try_get() and the while loop load in wait() memory_order_acquire felt appropriate since we would get synchronization participation for both owing to seq_cst in publish. I used memory_order_relaxed in the load in wait() thats after the while loop because from participation in seq_cst synchronization, before happens ordering of store to this relaxed load would have been ensured.

its asked "what can a reader observe if the store that marks "published" were `memory_order_relaxed`? Why might the assert in Test 1 still pass on x86 while TSAN reports it?" - The assert with the 4 threads calling wait did not pass, so the premise of the question is false and TSAN did report a ton of races happening. though i think the idea was to question my understanding of the operation order between store and load with relaxed semantics and the answer is that since we have relaxed, the atomicity of the store is guaranteed and modification order among threads is guaranteed, however, other threads by virtue of no happens before, might not know that publish is happening on the other thread and might try to read or might find out too late that the data was written and that is the issue with using relaxed which triggers the TSAN warnings.

As to the last part of the question, a reader, that observes a publisher to be writing, ought to wait until the write is done to avoid UB and torn reads.

### Beyond the book (optional, ungraded; not queued unless you say "queue it")

The book's cost claims are framed for x86 vs. "weakly ordered" machines in general. On **AArch64 (ARMv8+)**, the dedicated load-acquire/store-release instructions (`LDAR`/`STLR`) already meet the requirements for `seq_cst` loads and stores. So a seq_cst load/store there costs about the same as acquire/release, and the bigger savings come from `relaxed`. On x86-64, a seq_cst store compiles to `XCHG` (or `MOV` + `MFENCE`), while a release store is a plain `MOV`. That's the "small additional cost on stores" the book mentions. If a trading system ever runs on ARM servers, measure before hand-tuning orders.

## Review: 5.3.3 (2026-10-01)

Confidence tags: Q1 and Q5 have them (med). Q2, Q3, Q4 and all the warm-ups don't.

### Warm-up

- **4.1-notify-outside-lock -> hit.** > "when `wait(lk, pred)` is first called, it evaluates the predicate while holding the lock. If the producer has already pushed an order, `!orders.empty()` is true, so `wait()` returns immediately". Case (ii) is also right: wake, reacquire, recheck. This is the first hit on this misconception.
- **5.1-data-race-definition -> miss.** > "two or more threads simultaneously access the same memory location, at least on of them is a write operation, and there is no enforced order between the operations." That's the third time the condition **"at least one of the accesses is not atomic"** is missing. The verdict ("not a data race") is right, and this time you credit atomicity rather than ordering, which is progress. But the question asked for the definition and the deciding condition. "the compiler decides to put one in front of the other" is also not the reason. Relaxed stores have no cross-thread ordering at all, and they're legal only because both accesses are atomic.
- **5.2-cas-failure-ordering -> miss.** (a) acq_rel → acquire is right, and so is (b) release → relaxed. But:
  - > "If the success memory order was given `memory_order_seq_cst` the the failure case memory order is set to be `std::memory_order_acquire`": wrong, it stays `seq_cst`. Only release and acq_rel get demoted.
  - (c) > "the book forbids using `memory_order_release`, `memory_order_seq_cst`, `memory_order_acq_rel`": `seq_cst` is allowed as a failure order. Only `release` and `acq_rel` are forbidden.
  - (d) The book's rule is "the failure order can't be stronger than the success order". Your 2026-09-26 correction had it; this answer doesn't.

### Quiz

**Q1 -> hit.**
- (a) > "all four atomic operations participate in one single total order … If monitor 1 sees halt_a == true and then sees halt_b == false, the total order requires A's store to precede B's store". This is the book's argument.
- (b) > "unlike the `seq_cst` case, it is possible for both to print … release/acquire only gives synchronization for the specific release/acquire pair that actually communicates through that atomic variable". Correct.
- (c) Correct: relaxed loads drop out of the total order. The book's line is "you must use sequentially consistent operations on all your threads to get the benefit."

Nit: "It is also possible for `venue_b` to execute before monitor 1's `halt_b.load()`, in which case monitor 1 sees `true`." Under release/acquire, "executed earlier" doesn't force the load to see it. That's the interleaving model the book says to throw out (see Q3).

**Q2 -> hit.** All five parts are right, with the right reasons:
- (a) 101.
- (b) and (c): 0 or 101, because "observing `true` does not establish the release/acquire synchronization because the store that wrote `true` was relaxed".
- (d) `false` gives 0 or 101; `true` gives 101.
- (e) A plain `int` is a data race, so UB.

This is exactly the model that `5.3-sync-needs-value-read` probes, and here you got it fully right. That item is due today but wasn't a warm-up pick, so I haven't logged it. The next warm-up will confirm it.

**Q3 -> miss.**
- (a) > "threads don't have to agree on order of events … threads only need to agree on modification of the atomic variables". The requirement half is right. The two models to throw out are **(1) operations from different threads neatly interleaved, and (2) the compiler or processor reordering instructions**, and they weren't named. The cache-coherence explanation that follows is the hardware model the book says to drop in favour of the per-variable modification order (the man-in-the-cubicle picture).
- (b) > "A, B and D are possible … the z-incrementer … isnt possible". C is correctly impossible: it wrote 2 in iteration 1, so its own later read can't return 1. **A is also impossible:** the x-incrementer reads `y` as 2 and then as 1. Once a thread has seen a value of a variable, it can't later read an earlier one, and that applies to **every** variable it reads, not just the one it writes. You only checked each thread's own variable. B and D are possible.

**Q4 -> hit.** All five rows are right:
- relaxed: neither read is guaranteed;
- acquire: (i) yes, (ii) no;
- release: (i) no, (ii) yes;
- acq_rel and seq_cst: both yes.

Nits: the relaxed row reasons through "immediate observability" and caches. The book's reason is simpler: a relaxed RMW doesn't take part in synchronizes-with. Also, (i) under `release` is a **data race** (UB), not a "race condition".

**Q5 -> hit.** > "`mutex::lock()` is … acquire, `mutex::unlock()` is … release … the same mutex needs to be used … exclusive access, which serializes thread access to the protected critical section". This is the book's point, and it's the right model after last worksheet's Q3 miss.

### Build

```
publish_once: TSAN 4/4 clean | ASan+UBSan 4/4 clean | 2-TU link OK   (run 4 times = 16/16/OK)
```

`verify.sh` takes about 150 s for `-n 4` on this driver: 10 spinning threads under TSAN on the sandbox's 2 cores, about 25 s per TSAN run. The tool call limit is about 3 minutes, so I ran four batches of 4. A fifth batch was interrupted. The real total is 16 TSAN runs and 16 ASan+UBSan runs, all clean, and the link check is OK. That's short of the 20-run gate. Run `tools/verify.sh publish_once` once locally for the full 20.

**Library: the ordering is correct, but there's a real leak.**
- **Ordering:** correct. `new T(value)` is an argument, so it's evaluated before the CAS (5.3.2 Q4(c)), and the construction is sequenced before the successful CAS, which is at least a release. The loads are acquire, so a reader that sees the non-null pointer synchronizes-with the CAS and sees the whole `T`. `wait()`'s second, relaxed load is fine: the pointer never changes once set, and that thread has already acquired. It would be simpler to keep the pointer from the loop.
- **Leak (fix item F11a).** Every `publish()` call allocates `new T(value)` *before* the CAS. Losers never delete theirs, and there's no destructor, so the winner's `T` leaks too. I ran your driver once in scratch under LeakSanitizer: **1,802 objects / about 7.2 MB leaked**. Of those, 1,600 come from Test 2 (8 publishers × 200 rounds), 200 from Test 1's winners, and 2 from Test 3. `verify.sh` runs ASan with `detect_leaks=0`, which is why the gate didn't see it. Fix: on a failed CAS, `delete` your allocation (or don't allocate until you've won), and add a destructor that deletes the published `T`. The class is already non-copyable, so the destructor is safe.
- **Weakest ordering (fix item F11b).** The spec asked for the weakest correct order, with a comment justifying it. The CAS uses `seq_cst`, justified by "I want immediate visibility of this change made to all threads". Memory orders don't make anything visible *sooner*, and a stronger order doesn't make the CAS any more exclusive. Exactly one CAS from `nullptr` can succeed because the RMW is atomic and the variable has one modification order. That holds even with `relaxed`. The weakest correct choice is **release on success** (it publishes the construction) and **relaxed on failure** (a loser reads nothing through the pointer and returns `false`). The loads' comments ("to immediately sync") have the same "immediacy" framing. Acquire is right, but the reason is "if I read the published pointer, I synchronize-with the release and see the construction".
- Nits: no include guard or `#pragma once`; `<iostream>` is unused; `new T(value)` copies (`std::move(value)` would do).

**Driver vs. the Tests-to-write list:** all three tests are written, and every listed assert can fail in principle. Notes, not fix items:
- Test 2 checks the published object's internal consistency (`arr[j] == value/2`) rather than recording which thread won and comparing against that thread's value. It still catches the torn-overwrite case, and the count assert catches two winners.
- Test 3 compares addresses from one thread only. Test 1 doesn't compare addresses across readers.

**Teeth check, and a correction to my own spec.** The spec said TSAN would report a weakened release. I had verified that on a flag-plus-array design, not on your pointer design. In scratch I made every ordering in your header `relaxed` (CAS and all three loads):
- native `-O2`: asserts fired **0/5** runs;
- TSAN: **0/4** runs reported anything.

So on this machine, neither the asserts nor TSAN show this driver has teeth against a relaxed pointer publish. I haven't pinned down why TSAN misses it here, and I haven't spent your time on it. Your self-explanation says that with relaxed "the assert … did not pass … and TSAN did report a ton of races". I couldn't reproduce that with relaxed on the CAS and loads, so you may have weakened a different version. That isn't a mark against you; it means the ordering in this build is checked by reasoning, not by the tools. Don't treat the clean gate as proof of the orders.

**Self-explanation: a miss on the ordering reasons.**
- > "i used memory_order_seq_cst because … i wanted to guarantee that all other threads must be able to see the publish right before they do their operations … if i had just used acq_rel then the other threads … might not register the publishing because unlike mutex, acq_rel CAS isnt exclusive access". Both reasons are wrong. Ordering doesn't control *when* other threads see the store, and exclusivity comes from the CAS being atomic, not from `seq_cst` (see F11b). The acquire loads are justified correctly ("synchronization … owing to [the release side of] seq_cst in publish").
- On the relaxed store: > "other threads … might find out too late that the data was written". The precise answer: a reader can see the **non-null pointer** with no happens-before from the construction, so its reads of the `T` fields race with the constructor's writes. That's a data race (UB), and in practice possibly stale fields. It's not "finding out late".
- Last part: correct in spirit. Your pointer design never exposes a "writing" state, which is a good way to sidestep it.

### Queue updates

```
python3 tools/review.py log 4.1-notify-outside-lock hit --note "5.3.3 warm-up: wait(lk,pred) checks pred under the lock before sleeping; early notify harmless; no lost wakeup"
python3 tools/review.py log 5.1-data-race-definition miss --note "5.3.3 warm-up: definition again omits 'at least one access not atomic'; verdict (relaxed stores not a race) right, attributed to atomicity + 'compiler puts one in front'"
python3 tools/review.py log 5.2-cas-failure-ordering miss --note "5.3.3 warm-up: seq_cst->acquire (wrong, stays seq_cst); listed seq_cst as forbidden failure order; no 'failure no stronger than success' rule"
python3 tools/review.py add 5.3 5.3-relaxed-models-and-coherence "Leaving seq_cst: which two mental models does the book say to throw out, what's the only thing threads must still agree on, and why can't one thread see ANY variable go backwards (e.g. y: 2 then 1)?" --kind miss --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.3 Q3"
python3 tools/review.py add 5.3 5.3-seqcst-total-order "Two writers, two observers reading the flags in opposite order: why can't they disagree under seq_cst, why can they under release/acquire, and what happens if one observer uses relaxed?" --kind core --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.3 Q1"
python3 tools/review.py add 5.3 5.3-rmw-acquire-release-halves "An RMW with acquire / release / acq_rel / seq_cst: which half synchronizes with a prior release store, which with a later acquire load, and what does each order publish or receive?" --kind core --source "synchronizing-operations-and-enforcing-ordering-notes.md 5.3.3 Q4"
```

Only two core items. The pairing and "must read the value" concept is already the `5.3-sync-needs-value-read` misconception, and mutex acquire/release is covered by `5.3-mutex-sync-internal-atomics`. More core items come when 5.3.3's consume part closes.

### Fix list

- **Opened F11** (`publish_once`): (a) leak: delete the loser's allocation (or allocate only after winning), and add a destructor; (b) weakest orders with correct justification comments: success release, failure relaxed, loads acquire; (c) rewrite the self-explanation's ordering reasons. Re-verify with `verify.sh` plus one LeakSanitizer run (`ASAN_OPTIONS=detect_leaks=1`).
- No rows closed.

### Beyond the book (ungraded)

- `verify.sh` disables leak detection (`detect_leaks=0`), so a leak like F11a passes the gate. Worth turning on for builds that own heap memory, or adding an LSan pass. That's your call, since it changes the gate for the older builds too.
