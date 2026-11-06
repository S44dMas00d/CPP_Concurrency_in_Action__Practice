# 6.1 — What does it mean to design for concurrency?

## 6.1, 6.1.1 — Thread safety vs. genuine concurrency; guidelines for designing data structures for concurrency

```
$ python3 tools/review.py due   (2026-10-06)
Due now: 74 active (73 overdue) | maintenance due: 1 | due in next 3 days: 0 | total active: 74, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.3-sync-needs-value-read  (sec 5.3, 5d overdue, streak 0)
     source: synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(b)(c)
  2. [misconception] 5.2-cas-weak-vs-strong  (sec 5.2, 4d overdue, streak 1)
     source: atomic-operations-and-types-notes.md 5.2.3 Q2
  3. [misconception] 5.1-data-race-definition  (sec 5.1, 3d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (5.1 Q3 missed 'at least one not atomic')
Also due (next worksheets): 71 more (4.1-notify-outside-lock, 5.2-cas-update-loop, 5.2-memory-order-by-operation, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 11 (F1-F11)
Lint: 18 items more than 7 days overdue (oldest 3.2-deadlock-both-locks-held, 11d) -> review-only worksheet recommended
Git: clean
```

Process note: this worksheet goes against three written rules. (1) Book order: 5.3 is still `current`, and the rest of 5.3.3 plus 5.3.4 onward haven't been read. (2) The framework's review-only trigger: 18 items are more than 7 days overdue. (3) Your plan to clear the fix list (F1–F11) before ch6. Issued because you asked for 6.1 explicitly. I haven't changed any rule files; if this is the reading order you described on 2026-10-05, confirm it and I'll update the framework.

Triage:
- Definition of thread-safe (self-consistent view; no lost or corrupted data; invariants upheld; no problematic race conditions), and "safe only for particular types of concurrent access" with its two shapes: **recall** (Q1).
- Designing for concurrency vs. merely being thread-safe: a mutex serializes; the smaller the protected region, the less is serialized: **recall** (Q2).
- The four safety guidelines (from ch3): invariants never visible broken, complete operations rather than steps, exception safety, deadlock avoidance through lock scope and no nested locks: **code-tracing** (Q3). Spotting them in an interface is the skill; listing them isn't.
- Constraints on users: constructors and destructors need exclusive access, and the user has to keep everyone out until construction finishes and once destruction starts. For assignment, `swap()` and copy construction, the designer decides between "safe concurrently" and "caller guarantees exclusive access": **recall + code-tracing** (Q4).
- **No build.** 6.1 contains guidelines and no mechanism or listing. The builds for this chapter are 6.2 (stack/queue extensions) and 6.3 (lookup table).
- Chapter 7 (lock-free) is only mentioned in passing. Not quizzed.

Add a low/med/high confidence tag to each quiz answer.

### Warm-up

1. The market-data writer thread publishes a snapshot like this. `ready` is `std::atomic<bool>`, initially `false`, using default orderings; `snap` is a plain struct.

```cpp
// writer
snap.bid = 101.5;
snap.ask = 101.7;
ready.store(true);

// reader
double b = snap.bid;            // (r1)
if (ready.load()) {             // (r2)
    double a = snap.ask;        // (r3)
} else {
    double a2 = snap.ask;       // (r4)
}
```

   Assume the writer and reader run concurrently. For each of r1, r3 and r4, say whether the writer's write to that field is guaranteed to happen-before the read. Name the relation that's present or missing. What does the program have if it's missing?

Answer: 

The key is that the `ready` atomic only creates a synchronization relationship when the reader's `ready.load()` **observes the writer's `ready.store(true)`**. The fact that one thread happens to execute earlier in wall-clock time is not, by itself, a happens-before relationship.

For `r1`:

```cpp
double b = snap.bid;   // r1
```

there is **no guaranteed happens-before relationship** from the writer's `snap.bid` write to `r1`. The read occurs before the reader performs the `ready.load()`, so it cannot benefit from the synchronization established by that load. If the writer's write and `r1` are potentially concurrent, this is an unsynchronized read/write of a plain object and therefore constitutes a **data race**, resulting in undefined behavior.

For `r3`:

```cpp
if (ready.load()) {    // r2
    double a = snap.ask; // r3
}
```

there **is** a happens-before relationship, provided `r2` observes the writer's `ready.store(true)`.

The writer performs:

```cpp
snap.bid = 101.5;
snap.ask = 101.7;
ready.store(true);
```

The `ready.store(true)` is a release operation under the default `std::memory_order_seq_cst`, and the `ready.load()` is an acquire operation. If the load observes that store, they synchronize:

```text
snap.ask write
      ↓
ready.store(true)
      ↓ synchronizes-with
ready.load() == true
      ↓
snap.ask read (r3)
```

Therefore the writer's writes before the store happen-before the reader's operations after the load. So the write to `snap.ask` happens-before `r3`, and there is no data race on that access.

For `r4`:

```cpp
else {
    double a2 = snap.ask; // r4
}
```

there is **no such guarantee**. Reaching the `else` branch means `ready.load()` returned `false`, so it did not observe the writer's `store(true)`. Therefore there is no synchronizes-with relationship connecting the writer's `snap.ask` write to `r4`.

If the reader executes before the writer, for example:

```text
reader: ready.load() -> false
reader: r4

writer: snap.ask = 101.7
writer: ready.store(true)
```

the reader cannot observe a store that has not happened yet, so there is no synchronization through `ready`. If the accesses are potentially concurrent, the unsynchronized plain read/write constitutes a data race.

So the complete answer is:

* **r1:** No happens-before. The read occurs before the synchronizing `ready.load()`. If the accesses are concurrent, this can be a data race.
* **r3:** Yes, **if `ready.load()` observes `ready.store(true)`**. The store/load synchronize, causing the writer's earlier `snap` writes to happen-before `r3`.
* **r4:** No. The load returned `false`, so it did not synchronize with the writer's `store(true)`. A concurrent write/read of `snap.ask` is therefore a data race.

The important principle is:

> **`seq_cst` does not mean "everything that happened earlier in real time is automatically visible." The happens-before relationship comes from synchronization, and here that synchronization requires the `ready.load()` to observe the `ready.store(true)`.**

2. A teammate has this update loop on `std::atomic<Px> best`:

```cpp
Px expected = best.load();
Px desired;
do {
    desired = combine(expected, incoming);
} while (!best.compare_exchange_weak(expected, desired));
```

   They switch it to `compare_exchange_strong` "so it never fails spuriously". (a) What is a spurious failure, and what about some machines makes it possible? (b) What does the switch cost in *this* loop? (c) When would you keep `strong` instead?

Answer: 

(a) compare_exchange_strong and compare_exchange_weak are CAS (Compare and Swap) operations and are cornerstone of working with std::atomic<>. compare_exchange_strong will compare the expected value with the value in the atomic and if they match the it `must` execute the swap with desired and return true, if the expected and current value of the atomic dont match then it `must` not swap the values but rather it makes expected equal to the current value stored in atomic and returns false. Now when working with the _weak flavor there is a failure mode where it might return false, even if expected and current value in atomic had matched, so in this case _weak will return false and will have also not updated the expected value and this is a spurious failure case. Now we get into why this happens.

in some architectures like ARM and RISC-V, there isnt a single compare and swap instruction, instead there is a pair of instructions needed to do the job:
- LL (Load Linked), read a value and register a watch on that address
- SC (Store Conditional), write to that address if nobody has touched the address since LL.

if another address has written to the address between LL and SC then the SC or write part doesnt go through. So a simple way of looking at this is that read and write a seperate instructions in some architectures. For their _strong CAS implementations, they have an internal retry loop which ensures that the SC goes through before the boolean result is returned, where as _weak expects you to handle the check for if the write went through in your external loop as well. This also leads to part b:

(b) the general cost of using compare_exchange_strong vs. compare_exchange_weak is that we have the additional overhead of the internal retry loop of _strong in addition to our own retry loop - making a sort of nested loop situation. However, in this case, with _weak we are going to have repeated calls to this line `desired = combine(expected, incoming);` and this makes all the difference because if this call isnt cheap, i.e., recalculating desired on each spurious fail can be expensive as compared to the internal retry loop of _strong, or it the call could  be cheap compared to the retry loop, a definitive answer can only be achieved through benchmarkingg but my intuition leans towards _strong as well.

(c) In principle along, there are three reasons you use a compare_exchange_strong:
- you just want to know if this thread won the race or another one and you dont have a retry loop - just an operation.
- you dont want to write a manual retry loop
- the calculation of desired value in loop body is expensive to repeat.

for the third reason - which i feel like is applicable to this given loop, i am inclined to use _strong. weak is designed for retry loops because spurious failures are harmless when you're going to retry anyway. strong is useful when a CAS failure needs to represent a genuine value mismatch, especially for one-shot CAS operations or when retrying would require expensive recomputation. In this particular loop, strong may be advantageous because a spurious weak failure causes combine() to be executed again.


3. Two cases in a trading system. (i) Four worker threads call `fills.fetch_add(1)` while a stats thread calls `fills.load()` on the same `std::atomic<long> fills`. (ii) A stats thread reads a plain `long total_fills` while one worker thread does `total_fills += n` on it, with no lock and no other ordering. Give the Standard's definition of a data race, with every condition. Classify each case, and name the condition that decides it.

Answer: A data race occurs when two threads access the same memory location concurrently, at least one access is a write and there is no enforced ordering between them. 

now for the case classification:

(i) this will not be a data race on std::atomic<long> fills because std::atomic ensures atomicity of all operations exposed in the interface, i.e., i cant speak for which of the above mentioned operations will happen when in relation to the others, however, I can comfortably say that for atomic variable like fills, they will not overlap in their execution.

(ii) this will be a data race since there are two threads, no mention of atomic variables after or before and no other primitives like mutexes being used. this is very likely going to be a data race since we have possibility if stats thread reading while worker thread is writing to total_fills and since there is no ordering between them as per the question, it will lead to UB.


### Quiz

1. (Recall) By the text's definition, what does it take for a data structure to be *thread-safe*? List what each thread must see, and the three things that must not happen. The text then says a structure is generally safe "only for particular types of concurrent access". Describe the two shapes this can take.

Answer: for a data structure to be thread safe, there are four guiding principles:

(a) Never expose broken invariants: while one thread makes a change that temporarily breaks the invariants of the data structure, no other thread should be able to see the broken invariants, i.e., either protect the code that makes such changes under a lock or make the interface such that such invariants are never exposed.

(b) Provide complete operations and not steps needed to do operations: this points towards the interface design point that a race window between mutex protected operations is still possible so such operations should be removed or combined or more broadly the interface should be designed in a way to avoid the provision of operations whose interleavings could result in race conditions.

(c) Exception Safety: if code for some operation throws midway, there should be ordering in the instructions such that any data loss or corruption is avoided and despite the possible exceptions, the data structure remains valid and usable. this points to where if we are doing a pop() operation on the stack and while making a heap allocation, we dont find enough resources and throw then the object ought not be deleted unless the heap allocation was successful inorder to ensure no loss of data. Also use a RAII bolt on for mutexes  used to ensure unlock for all possible paths, happy and exception paths.

(d) Minimize deadlock opportunities: minimize the lock scopes to absolutely necessary lines, and avoid acquiring multiple locks, if unavoidable, use deadlock prevention techniques like locking in consistent order or using mutex hierarchy.

A data structure is thread-safe if, when multiple threads access it concurrently, each thread must see a valid state of the data structure, and all modifications must be properly synchronized so that the structure's invariants are maintained.

Specifically, each thread must see:

No broken invariants — the data structure must always appear to be in a valid state.
No lost updates — one thread's modification must not accidentally overwrite or lose another thread's modification.
No corruption — concurrent operations must not leave the data structure in an invalid or corrupted state.

The text then points out that being thread-safe does not mean that every possible combination of operations can safely be performed concurrently. A data structure is generally thread-safe only for particular types of concurrent access.

The two common shapes are:

- Concurrent access by different operations: different operations can safely execute concurrently, for example one thread performing a read while another performs some independent operation, provided the data structure's synchronization guarantees support that combination.

- Concurrent access by the same operation: multiple threads can safely perform the same operation concurrently. For example, several threads may be allowed to perform push() concurrently, with the data structure synchronizing those modifications correctly.

So the important distinction is that thread-safe describes the correctness of supported concurrent access, not that every operation is automatically safe to combine with every other operation.


2. (Recall) The text says truly designing for concurrency "means more than" being thread-safe. What is the extra requirement? Why does a mutex work against it by its nature, and what is that effect called? Give, in one sentence, the single idea the text says should guide every design question in 6.1.1.

Answer: Designing a data structure for concurrency means more than simply making it thread-safe. Thread safety means that multiple threads can access the structure without causing data races, corruption, or undefined behavior. But a truly concurrent design should also provide opportunities for multiple threads to make progress simultaneously when their operations do not actually conflict.

This is where the tension between thread safety and concurrency comes in. A simple way to make an entire data structure thread-safe is to put one coarse-grained mutex around it and have every public operation acquire that mutex, for example with a `std::lock_guard`. This is safe because only one thread can access the protected structure at a time, but it also effectively **serializes** all operations. Even if two threads are performing completely independent operations that could safely happen at the same time, one has to wait for the other.

That waiting is **contention**: multiple threads are competing for the same synchronization resource. The mutex itself is doing exactly what it is designed to do—providing mutual exclusion—but that mutual exclusion can work against concurrency because it prevents simultaneous progress. If the entire structure is protected by one mutex, adding more threads may provide little benefit because they spend much of their time waiting rather than doing useful work. There is also additional synchronization and scheduling overhead.

Designing for concurrency therefore means looking for opportunities to reduce unnecessary serialization. For example, a data structure might use **finer-grained locking**, where different mutexes protect independent parts of the structure. Then two threads operating on different regions may be able to proceed simultaneously. More sophisticated designs might use reader-writer locks, atomics, lock-free techniques, or other synchronization mechanisms where appropriate. The tradeoff is that increasing concurrency generally makes the implementation more complicated and can introduce additional problems such as deadlocks or more difficult correctness reasoning.

So the important distinction is that **thread safety asks whether concurrent access is correct, while concurrency asks how much useful work can actually proceed concurrently**.

The single guiding idea from the text is:

> **When designing a concurrent data structure, always ask: "What can be done concurrently?"**


3. (Code-tracing) A first draft of a price table:

```cpp
class price_table {
    mutable std::mutex m;
    std::map<std::string, double> prices;
    std::size_t updates = 0;     // invariant: number of successful set() calls
public:
    bool contains(const std::string& sym) const {
        std::lock_guard<std::mutex> lk(m);
        return prices.count(sym) != 0;
    }
    double get(const std::string& sym) const {
        std::lock_guard<std::mutex> lk(m);
        return prices.at(sym);              // throws std::out_of_range if absent
    }
    void set(const std::string& sym, double px) {
        std::lock_guard<std::mutex> lk(m);
        ++updates;
        prices[sym] = px;                   // may allocate, so may throw
    }
    void erase(const std::string& sym) {    // venue delists a symbol
        std::lock_guard<std::mutex> lk(m);
        prices.erase(sym);
    }
    void copy_from(const price_table& other) {
        std::lock_guard<std::mutex> lk(m);
        std::lock_guard<std::mutex> lk2(other.m);
        prices = other.prices;
        updates = other.updates;
    }
};

// strategy thread
if (table.contains(sym)) use(table.get(sym));

// two other threads, concurrently
a.copy_from(b);      // thread X
b.copy_from(a);      // thread Y
```

   Find three problems, each breaking a different one of the four safety guidelines in 6.1.1. For each: (a) the guideline it breaks, (b) the interleaving or event that exposes it, and (c) a one-line fix using ch3 tools.

Answer: Here are the three problems in this snippet that i could identify:

The first problem is that the interface does not provide **complete operations**, because it forces the caller to combine individually thread-safe operations into a larger logical operation. `contains()` and `get()` are each protected by the mutex, so there is no data race inside either function, but the protection does not extend across the two separate calls. For example, in `if (table.contains(sym)) use(table.get(sym));`, thread A can call `contains()` and get `true`, then thread B can call `erase(sym)` before thread A calls `get(sym)`. Thread A will then call `get()` and receive an `std::out_of_range` exception. This is a race in the logical operation, even though the individual functions are thread-safe. The guideline being violated is therefore **"provide complete operations, not operation steps."** The fix is to provide a single operation that performs the whole logical action while holding the mutex, for example:

```cpp
std::optional<double> try_get(const std::string& sym) {
    std::lock_guard<std::mutex> lk(m);

    std::map<std::string, double>::const_iterator it = prices.find(sym);

    if (it == prices.end())
        return std::nullopt;

    return it->second;
}
```

Now the check and retrieval happen under one lock acquisition, so another thread cannot erase the element between those two steps. The exact interface should depend on what operation the caller actually needs; the important idea is that the class should provide the complete logical operation rather than forcing the caller to construct it from separately synchronized methods.

The second problem is the locking order in `copy_from()`. The function first locks `this->m` and then locks `other.m`:

```cpp
std::lock_guard<std::mutex> lk(m);
std::lock_guard<std::mutex> lk2(other.m);
```

This becomes dangerous when two threads execute `a.copy_from(b)` and `b.copy_from(a)` simultaneously. One possible interleaving is that thread X locks `a.m`, while thread Y locks `b.m`. X then tries to lock `b.m` and has to wait for Y, while Y tries to lock `a.m` and has to wait for X. Neither can proceed, giving us a classic **circular wait and deadlock**. This violates the guideline of **minimizing opportunities for deadlock**. The Chapter 3 fix is to acquire both mutexes using `std::lock()`, which uses a deadlock-avoidance algorithm:

```cpp
void copy_from(const price_table& other) {
    std::lock(m, other.m);

    std::lock_guard<std::mutex> lk(m, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(other.m, std::adopt_lock);

    prices = other.prices;
    updates = other.updates;
}
```

The important part is that we don't independently acquire the two mutexes in an order that can be reversed by another thread. Converting the method into a copy constructor which only locks the mutex of `other` is also reasonable, but that is more of an interface/design consideration; it doesn't by itself solve the deadlock problem for an existing object's `copy_from()` operation.

The third problem is in `set()`, and this one concerns **exception safety and maintaining invariants**. The class explicitly states that `updates` has the invariant that it represents the number of **successful** `set()` calls:

```cpp
++updates;
prices[sym] = px;
```

The problem is that `prices[sym] = px` can throw, for example if inserting a new element requires an allocation and that allocation fails. If `++updates` has already happened, but the map operation then throws, the function has not successfully completed, yet `updates` has still been incremented. For example, if `updates` was 10, `++updates` makes it 11, and then `prices[sym] = px` throws, we end up with `updates == 11` despite having only 10 successful `set()` calls. The invariant has therefore been broken.

The simple fix is to perform the potentially throwing operation first and only update the bookkeeping after it succeeds:

```cpp
void set(const std::string& sym, double px) {
    std::lock_guard<std::mutex> lk(m);

    prices[sym] = px;
    ++updates;
}
```

If the map operation throws, `++updates` is never reached, so the invariant remains valid.

4. (Recall + code-tracing) Every public member of `price_table` above locks `m`. (a) What does the text say about constructors and destructors: what access do they need, and whose job is it to ensure it? Use that to explain why locking `m` in the destructor wouldn't make `~price_table` safe against a thread still calling `get()`. (b) Suppose you add a member `swap(price_table&)`. What decision does the text say you must make about it (and about assignment and copy construction)? Name both options, and say what each one asks of the caller.

Answer:

(a) Constructors and destructors are special because they require **exclusive access to the object**, and it is the caller's responsibility to ensure that no other thread accesses the object while construction is taking place or after destruction has begun. The class itself cannot make this safe simply by putting a mutex inside the object.

For example, if another thread is executing `table.get("BTC")` while another thread starts destroying `table`, the program already has an object-lifetime problem. Putting a lock in the destructor does not solve it:

```cpp
~price_table() {
    std::lock_guard<std::mutex> lk(m);
}
```

The mutex only protects operations that actually synchronize through that mutex; it does not make it legal for another thread to access the object while its lifetime is ending. In fact, destruction eventually destroys the mutex itself, so the mutex cannot be relied upon as a mechanism for protecting the object's entire destruction process. The caller therefore has to ensure that all threads using the object have stopped accessing it before destruction begins, typically by stopping and joining those threads.

This is different from the ordinary member functions. For example, `get()` can lock `m` because the `price_table` object is already alive and its mutex is valid. During destruction, however, the validity of the entire object and its members is what is at stake.

(b) For operations such as `swap()`, assignment, and copy construction, the designer has to decide what concurrency guarantees the operation provides. There are essentially two choices: either make the operation itself safe to use concurrently with the other supported operations, or require the caller to provide exclusive access when using it.

If we choose to make `swap(price_table&)` internally thread-safe, it has to safely synchronize access to **both** `price_table` objects. Since two threads could potentially execute:

```cpp
a.swap(b);   // thread X
b.swap(a);   // thread Y
```

the implementation should avoid simply locking `a.m` and then `b.m` in that order, because the other call could acquire them in the opposite order and deadlock. A deadlock-avoiding mechanism such as `std::lock()` or `std::scoped_lock` can acquire both mutexes safely.

Alternatively, the class can specify that `swap()`, assignment, or copy construction requires **exclusive access from the caller**. In that design, the class does not promise that the operation can safely run concurrently with other operations, and the caller must ensure that no other thread is accessing the relevant object(s) during the operation.

So the overall distinction is:

"For ordinary operations, the class can provide internal synchronization. For constructors and destructors, the caller must guarantee exclusive access because the object's lifetime itself is being established or ended. For operations such as `swap`, assignment, and copy construction, the designer must explicitly decide whether the class provides concurrent safety or requires the caller to provide exclusive access."


### Beyond the book (optional, ungraded)

The text says a structure is usually safe only for some kinds of concurrent access. The standard library containers make exactly this kind of promise: concurrent calls to `const` member functions on the same object don't race, but any concurrent non-`const` call needs exclusive access. Which of the two shapes from Q1 is this? Name one non-`const` member of `std::map` that people wrongly assume is a read. Not queued unless you say "queue it".

Answer: 


---

## Review: 6.1, 6.1.1 (2026-10-07)

Confidence tags: none on any quiz answer. They're required (framework step 6) because they're how confident misses get found.

Out of order: 6.1 was done while 5.3 is still `current`, the review-only trigger is met, and F1–F11 are open. You haven't confirmed a new reading order yet, so the framework still says strict book order.

### Warm-up

- **5.3-sync-needs-value-read -> hit.** > "r1: No happens-before. The read occurs before the synchronizing `ready.load()` … r3: Yes, if `ready.load()` observes `ready.store(true)` … r4: No. The load returned `false`, so it did not synchronize-with". The consequence is right too: a data race, so UB. This confirms the 5.3.3 Q2 result. Nit: r1 *is* a data race whenever the write and read are concurrent. It's not "can be"; there's no ordering at all.
- **5.2-cas-weak-vs-strong -> hit.** > "in some architectures … there isnt a single compare and swap instruction" (a). > "the additional overhead of the internal retry loop of _strong in addition to our own retry loop - making a sort of nested loop" (b). > "the calculation of desired value in loop body is expensive to repeat" plus the one-shot case (c). All three match the book. Nits:
  - On a spurious failure, `expected` *is* written; it just gets the value it already had. This nit was also raised on 2026-09-26.
  - Leaning to `strong` *here* is fine only if `combine()` is expensive. The book's default for a loop is `weak`.
- **5.1-data-race-definition -> miss.** > "two threads access the same memory location concurrently, at least one access is a write and there is no enforced ordering between them." This is the fourth time **"one or both accesses is not atomic"** is missing. Case (i)'s verdict is right, but the reason given is > "they will not overlap in their execution". The Standard's reason is that both accesses are atomic, so the not-atomic condition fails. Case (ii) is right: one access is plain, so it's a race. Suggestion: write the 5.1 Q3 correction (ch5 corrections are still outstanding) as the book's one sentence, with all four parts in order.

### Quiz

**Q1 -> hit.**
- > "each thread must see a valid state of the data structure". This matches the book's "self-consistent view".
- > "No broken invariants … No lost updates … No corruption". This covers three of the book's four: no data lost or corrupted, invariants upheld. **"No problematic race conditions" is missing.**
- > "Concurrent access by different operations … Concurrent access by the same operation". Both shapes are named, but each only by its safe half. The point of each shape is the contrast with what is *not* safe:
  - Shape 1: many threads can do one operation concurrently, but **another operation needs exclusive access**.
  - Shape 2: different operations are safe concurrently, but **many threads doing the same operation is the problem**.

  Your closing line ("not that every operation is automatically safe to combine") gets the idea. Next time, name the restricted half.
- The opening four-guideline list answers a different question: that's 6.1.1's "how to make it safe", not the definition.

**Q2 -> hit.**
- > "a truly concurrent design should also provide opportunities for multiple threads to make progress simultaneously". This is the book's "providing the opportunity for concurrency".
- > "This is safe because only one thread can access … but it also effectively **serializes** all operations". This is the right term.
- > "Designing for concurrency therefore means looking for opportunities to reduce unnecessary serialization". This is the guiding idea.
- Nits:
  - The book's term for the effect is **serialization**. "Contention" is a fine word, but it's not the one the text names.
  - The blockquoted "single guiding idea from the text", > "always ask: 'What can be done concurrently?'", isn't in the text. The book's sentence is: *how can you minimize the amount of serialization that must occur and enable the greatest amount of true concurrency?* Don't put quotation marks around a paraphrase.

**Q3 -> hit.** All three problems, guidelines and fixes are right:
- **Complete operations:** > "thread A can call `contains()` and get `true`, then thread B can call `erase(sym)` before thread A calls `get(sym)`". Fix: one locked `try_get`.
- **Deadlock:** > "thread X locks `a.m`, while thread Y locks `b.m` … circular wait". Fix: `std::lock` + `adopt_lock`.
- **Exceptions / invariants:** > "`updates == 11` despite having only 10 successful `set()` calls". Fix: do the throwing operation first, then `++updates`.
- Nits:
  - `try_get` must be `const`, like `get` (that's why `m` is `mutable`).
  - `a.copy_from(a)`: `std::lock` on the same mutex twice is UB. Listing 3.6 guards it with `if (&other == this) return;`.

**Q4 -> hit.**
- (a) > "require **exclusive access to the object**, and it is the caller's responsibility … destruction eventually destroys the mutex itself". Right on both points.
- (b) > "either make the operation itself safe to use concurrently with the other supported operations, or require the caller to provide exclusive access". Both options are there, and so is what the second asks of the caller. The deadlock-safe locking of both objects is a good extra.

### Build

None. 6.1 triaged to quiz only (guidelines, no mechanism).

### Queue updates

```
python3 tools/review.py log 5.3-sync-needs-value-read hit --note "6.1 warm-up: r1 and r4 no happens-before (load didn't read the store / read sequenced before load) -> data race; r3 ordered only if load reads true"
python3 tools/review.py log 5.2-cas-weak-vs-strong hit --note "6.1 warm-up: spurious failure + no single CAS instruction; strong-in-loop = nested loop; strong when desired is expensive or one-shot"
python3 tools/review.py log 5.1-data-race-definition miss --note "6.1 warm-up: 4th time without 'at least one access not atomic'; case (i) credited to atomic ops 'not overlapping'"
python3 tools/review.py add 6.1 6.1-thread-safe-for-which-access "Thread-safe only for particular kinds of concurrent access: the two shapes (same op concurrently while another op needs exclusive access; different ops concurrently while the same op is serialized). Given an interface, which shape is it and what must callers avoid?" --kind core --source "designing-for-concurrency-notes.md 6.1 Q1"
python3 tools/review.py add 6.1 6.1-ctor-dtor-copy-swap-contract "Ctors/dtors need exclusive access and the USER ensures it (none before construction completes, none after destruction starts); why a member mutex can't protect its own object's destruction; for swap/assignment/copy-construction the designer picks concurrent-safe or caller-guaranteed exclusive access." --kind core --source "designing-for-concurrency-notes.md 6.1 Q4"
```

Results: `5.3-sync-needs-value-read` streak 1 (due 10-12); `5.2-cas-weak-vs-strong` streak 2 (due 10-19); `5.1-data-race-definition` streak 0 (due 10-09).

6.1 closes with this review, so it gets two core items. Q3's four safety guidelines are already covered by `3.2-interface-race-top-pop`, `3.2-deadlock-both-locks-held` and the 3.2 exception items. Q2 (serialization) is something a rereader wouldn't get wrong.

### Fix list

No rows opened or closed.

### Beyond the book (ungraded)

The item was left blank. Answer: the standard containers' guarantee is shape 1. Any number of threads can call `const` members concurrently, and any non-`const` call needs exclusive access. `std::map::operator[]` is the classic trap: it's non-`const` because it *inserts* a default value for a missing key, so two "lookups" with `[]` can race.
