# 5.2 — Atomic operations and types in C++

Large section, done subsection-by-subsection (same shape as 3.2/3.3/4.2) — this file covers the whole of 5.2 across sessions, one `##` block per subsection.

## 5.2.1 — The standard atomic types

Triage: pure API/contract surface — `std::atomic_flag`'s special status, the `is_lock_free()` distinction, the primary-template's reduced operation set for user-defined types, the return-value design rationale (a real behavioral gotcha, not just trivia), and the memory-ordering-argument categories (semantics deferred to 5.3, just the shape for now). No reusable mechanism derived in-text yet — **quiz-only**.

### Warm-up (tier (c) — cross-chapter design-judgment; still nothing numerically due at session 16: 4.1 due at 17, 2.3/3.2/4.3 due at 18/20/18, 2.1/2.2 due at 18/19)

You have a simple shared counter incremented by many threads on a trading system's hot path. Compare guarding it with a `std::mutex` (ch3) vs. using `std::atomic<int>`'s `fetch_add()` (just introduced). Both correctly avoid the data race — but what's the concrete mechanistic difference in what each one is actually guaranteeing, and on a latency-sensitive hot path, when would you actually reach for one over the other?

Answer: now that i know of atomic types and atomic operations, I would be more inclined towards reaching for atomic rather than mutex to protect the shared integer counter from a data race or a torn read. both mutex lock around the counter incrementing code and the atomic operation on the atomic_int are meant to do the same thing, i.e., avoid multiple writes on the same memory location and serialization of the thread's access of this memory location for reads and writes. that being said, mutex has more overhead because its a separate object that has to first be instantiated and locked before increment code to avoid other thread from going forward before those threads acquire a lock and then has to be unlocked below counter to give up the lock to another thread. An atomic_int on the other hand is meant to do the same thing however since int is a native type - there is likely a single CPU instruction to which the counter incrementing operation maps to and thus allowing for true atomicity and serialization across threads without the blocking and overhead that comes with mutex.

### Quiz

1. (Recall) `std::atomic_flag` is treated differently from every other atomic type in two specific ways stated in the text. What are they, and what is its *entire* operation surface (no more, no less)?

Answer (med): `std::atomic_flag` flag has a special status because it is the only atomic type that has been guaranteed by the standard to have true atomicity on a CPU instruction level, i.e., it truly does have a single CPU instruction underneath and is in a true sense and not just an effect sense, an atomic type. Its entire operation surface which I assume means the interface of this type, consists of `test_and_set()` and `clear` and it is initialized using the enum ATOMIC_FLAG_INIT which means false. calling test_and_set() sets the value to true while returning the its previous value which could have been false or true, clear() sets it back to false.

2. (Recall) What does `is_lock_free()` returning `true` vs. `false` actually tell you is happening under the hood when you perform an operation on that atomic object?

Answer (med): is_lock_free() returning true means that the operation called on that particular atomic type under this atomic template specialization maps to a single CPU instruction and therefore is truly atomic, does not need a internal lock like a spin lock and is, as the name suggests, lock free. 
is_lock_free() returning false means that the operation on this particular template specialization of the provided type does not map to a single CPU instruction and thus to achieve practical atomicity, i.e., other threads may only see this instance pre and post this operation, the operation needs an internal lock or mutex. Something like this:

```cpp
template<>
class atomic<Type>{
    Type instance;
    std::mutex mut;

    void store(Type newVal){
        mut.lock();
        instance = newVal;
        mut.unlock();
    }
}

```

3. (Application) The standard atomic types have no copy constructor or copy assignment operator, even though they support assignment from and implicit conversion to the underlying built-in type. Why can't "copy one atomic into another" itself be a single atomic operation, mechanically? What's a concrete practical consequence of this for how you'd store atomics in a container?

Answer (med): to have an atomic constructor or a assignment copy constructor, we would need two steps to be atomic:
- the read from the source needs to be atomic
- the write to the destination needs to be atomic.
but this isn't how the operations are designed, kind of reminds me of the push() pop() race window scenario. Anyways, this is what it would look like in code:
```cpp
std::atomic<int> a{5};
// race window here.
std::atomic<int> b = a;
```
so this is an interface design decision and to have an atomic copy assignment between two compatible atomic variables, we need to be more explicit with it:

```cpp
atd::atomic<int> b.store(a.load());
```

4. (Code-tracing / gotcha) The assignment operators on atomic types return the *value stored*, not a reference to `*this` (breaking the usual C++ convention). Similarly, the named member functions like `fetch_add()` return the value *prior to* the operation, not the new value. Precisely: what race would a reference-returning assignment operator open up that returning the stored value by value closes off?

Answer (med): usual assignment operators of non atomic types have T& in their return types and return *this in the implementation which is appropriate. to view the value after the assigment however you must do another operation like read or assign to another temp variable or something like this, this might not be a problem in single threaded code but when the same function is running accross multiple threads - its possible that there is a race condition between the two operations. named and unnamed assignment style operators like fetch_add() and += both add to the value of the atomic type, however, the former returns a copy of the value before addition and the latter returns a copy of the value after addition and this eliminates the need for a secondary operation to check values from the reference returned, i.e., its not two but one operation and therefore avoids a race condition.

5. (Recall) The primary `std::atomic<>` template (used for a user-defined type `T`) supports a strictly smaller operation set than the built-in specializations like `std::atomic<int>`. Name what the primary template supports, and name at least two things the built-in specializations get that it doesn't.

Answer (med): the primary / default template is the one which will be used for supporting user defined types in atomic<>, making additional functionality without knowing a type's particulars makes no sense, this is further clarified which we do see specializations for builtin types like in, float, etc, where their operations and purpose are clear.
```cpp
// default or primary template supports:
store();
load();
exchange();
compare_exchange();

// specialization for integer supports:
store();
load();
exchange();
compare_exchange();
++, --, +=, -=;
fetch_add();
fetch_sub();
```

6. (Recall) Operations on atomic types take an optional memory-ordering argument. Name the three operation-category buckets (store / load / read-modify-write) and, for each, which subset of the six `memory_order_*` values is actually valid to pass. What's the default if you don't specify one?

Answer (high): the three categories and their memory orderings are:
```
store(): relaxed, release, seq_cst;
load(): relaxed, consume, acquire, seq_cst;
read-modify-write(): relaxed, release, consume, acquire, seq_rel, seq_cst;
```
the default for all operations is memory_order_seq_cst.


7. (Application) The alternative type names (`atomic_int`, `atomic_uint`, etc., listed in table 5.1) may refer either to the corresponding `std::atomic<>` specialization directly, or to a *base class* of it, depending on the implementation. What's the concrete portability danger of mixing `std::atomic<int>` and `atomic_int` for the same conceptual value in one codebase?

Answer (low): Author states that there may be portability issues which may lead to failure in compiling or ending up associating to the wrong specialization. we know that int_64 may point to long on linux but long long on windows and while this may be a simple and impossible to get wrong platform / OS based difference, if we expand this over to all specializations for all internal types like int, float, double, char, etc, and we factor in the possible OS and architectural permutations, then we might end up with out atomic_unit for example, pointing to the wrong specialization.

Confidence tag each answer (low/med/high) before self-checking, per the calibration rule.

## Review — 5.2.1

Warm-up: correct and well-reasoned — the mutex-overhead vs. lock-free-CPU-instruction contrast is the right shape, and "reach for atomic on a hot path, mutex when the critical section covers more than one operation" is exactly the right practical takeaway once `fetch_add()` is in the toolbox. No corrections.

Q1 — two things need separating here. First, the `is_lock_free()` absence: the reason `atomic_flag` is the one type without that member isn't that it's "guaranteed to map to a single CPU instruction" — it's that its operations are *required* to be lock-free by the Standard, full stop, so there's nothing to query. Every other atomic type's lock-free-ness is implementation-dependent (hence they need the query); `atomic_flag`'s is guaranteed, so the query would be pointless. "Lock-free" and "single instruction" aren't quite the same claim — lock-free just means no blocking, usually backed by a single instruction on common hardware but that's not the definition. Second — and this part is missing entirely, not just imprecise — the book's actual second reason `atomic_flag` is special: because it's guaranteed lock-free, you can build a simple lock out of it, and that lock is what every *other* atomic type's operations can fall back to implementing themselves on top of. That "it's the basis everything else can be built from" fact is the second of the "two specific ways" the question asked for. The operation surface itself (`test_and_set()`/`clear()`, `ATOMIC_FLAG_INIT`, no copy/no assignment) is correct.

Q2 — correct, and the mutex-emulation sketch is exactly the right mental model for what `is_lock_free() == false` looks like under the hood. Same minor "single instruction" simplification as Q1 (a true lock-free instruction can also be a short sequence like load-linked/store-conditional, not always literally one opcode) — a nitpick, not a miss.

Q3 — the mechanical half is correct and the push/pop-race-window connection is a good transfer. But the question's second half — the concrete practical consequence for storing atomics in a container — wasn't actually answered. Since atomics are neither copyable nor movable, `std::vector<std::atomic<T>>` can't do what vectors normally do: `push_back` triggering reallocation, `resize`, `erase`/`insert` all need to move or copy existing elements, and atomics support neither. Practical workarounds: a fixed-capacity container that never reallocates (`std::array`, or a vector sized once up front and never resized), or a container of indirection (`std::vector<std::unique_ptr<std::atomic<T>>>`) so the atomics themselves never move, only the pointers do. Small aside, not conceptually important: `atd::atomic<int> b.store(a.load());` has a typo and isn't valid syntax as written — `std::atomic<int> b; b.store(a.load());` is what was meant.

Q4 — clean and precise: correctly identifies the exact race window (a second, separate read after a reference-returning assignment) and why returning the value directly collapses it back to one atomic step. Nothing to add.

Q5 — correct and complete: right operation set for the primary template, the right (and more than the minimum two) extras for the built-in specializations, and the "can't know a user type's `+=`/`++` semantics generically" reasoning for why.

Q6 — store and load buckets both right. The read-modify-write bucket has one wrong name: `seq_rel` isn't a real value — the actual enumerator is `memory_order_acq_rel` (acquire-release combined, since an RMW op both reads and writes in one step, so it can carry both an acquire and a release semantic at once). Worth being precise here — these six names get used constantly in 5.3.

Q7 — the instinct (cross-platform typedef aliasing can silently diverge — `int64_t`/`long`/`long long`) is a real C++ portability hazard in general, but it's not the mechanism the book is actually describing here, so this is a genuine miss rather than a phrasing gap — good that it was flagged low-confidence. The actual danger: `atomic_int` may be implemented either as `std::atomic<int>` directly, or as a *base class* of it (the implementation's choice) — so mixing the two spellings for what's meant to be the same type across a codebase isn't guaranteed to give you the exact same type. That can break anything depending on exact type identity: template instantiations resolving to different specializations, overload resolution picking different candidates, or (in the base-class case) code implicitly relying on derived-to-base slicing behaving the same as two genuinely identical types. The fix in practice is what the book says directly: pick one spelling (`std::atomic<T>`) and don't mix.

**5.2.1 → passable, closed session 16.** Quiz-only, no build. Real misses worth remembering precisely: Q1's missing "basis for the other atomic types" fact, and Q6's `acq_rel` naming error. Q3 and Q7 each answered only half (or the wrong half) of what was asked — corrected directly above. Q2/Q4/Q5 all strong. No redo needed; 5.2's subsection loop continues to 5.2.2 whenever that's ready.

## 5.2.2 — Operations on std::atomic_flag

Triage: mostly fact/contract detail already adjacent to 5.2.1 (`ATOMIC_FLAG_INIT`, static-storage-duration guarantee, the exact 3-operation surface with per-operation memory-order restrictions, why it can't double as a general boolean flag) — quiz-only for that part. But the text also **derives a reusable mechanism in-text**: Listing 5.1's `spinlock_mutex`, a real `BasicLockable` type usable with `std::lock_guard<>`, built from nothing but `test_and_set()`/`clear()`. Per the triage rule ("any section that derives/builds something in the text itself... not just pre-flagged checkpoints"), that's a build task, not a quiz substitute — added to the framework's known-checkpoints list.

### Warm-up (tier (c) — cross-chapter design-judgment; still nothing numerically due at session 16)

`task_runner` (4.2.2) blocks its worker thread on a `condition_variable` until work is posted. The spinlock you're about to build instead busy-waits on `test_and_set()` in a loop. Design judgment: for a background worker thread that's typically idle for long stretches waiting on tasks, which of these two waiting strategies do you actually want, and why? What changes if the "critical section" being protected is instead just a two-instruction counter increment expected to be held for nanoseconds, not a wait that could stretch for seconds?

Answer: for a background worker thread that's typically idle for long stretches waiting on tasks - the obvious choice is the design used by task_runner, i.e., sleep until a task is put on the queue where by its awoken by the condition_variable.wait() and it executes the task on the queue.

if however the wait time isn't seconds but nano seconds then the overhead carried by the synchronization primitive condition_variable.wait() to put the thread to sleep and to wake it up, i.e., it takes some time to wake the thread back up after putting it to sleep and making it reacquire a core, if the protected section is just a  two-instruction counter increment expected to be held for nanoseconds - then all this overhead is not worth it since i suspect the increment has to be done repeatedly with nano-seconds between the critical section running again and again and so putting thread to sleep and waking it up is unnecessary overhead as compared to just keeping the thread busy-waiting on this section with a low overhead spin-lock.

### Quiz

1. (Recall) `std::atomic_flag` must be initialized with `ATOMIC_FLAG_INIT` — there's no default-constructed alternative. What guarantee does this initialization requirement combine with when the flag object has *static* storage duration, and why does that guarantee matter (what class of bug does it rule out)?

Answer (med): std::atomic_flag must be initialized using `ATOMIC_FLAG_INIT` because the standard needs to guarantee that the atomic_flah holds the same clear state before any thread touches it and it must be the case on any and all hardware running C++, i.e., exotic embedded systems, old compilers, whatever, the `ATOMIC_FLAG_INIT` maps to a system macro that allows the atomic_flag to be initialized with whatever represents the clear state on that system. static initialization is basically when a variable is defined using `static` or is defined at the global scope. if called by multiple threads, even initialization can be a race condition, since C++ 11, the static initialization of a variable in a function has been made race free so I assume the race condition for initialization bug for the atomic_flag gets ruled out, or that you never run into an uninitialized atomic_flag if it had static initialization since the standard has decreed that static atomic_flags get initialized with `ATOMIC_FLAG_INIT` at program start, before any thread touches the atomic_flag.


2. (Recall) `clear()` is a store operation; `test_and_set()` is a read-modify-write operation. What memory-order restriction does that place on `clear()` specifically — which of the six `memory_order_*` values are disallowed for it, and why does `test_and_set()` not have that same restriction?

Answer (med): here is the general rule - for load() or read operations, we need to `acquire` the atomic variable and so for such operations we can set `std::memory_order_acquire` - i like to imagine a pigeon coming in and you catch it `acquire` before trying to read the message in its paw. for read type operations, we cannot set `std::memory_order_release`.

for store() or write type operations, we need to `release` the atomic variable after we are done writing to it, I like to imagine the same pigeon but this time we write what all we need to to variables before the atomic and then write the atomic at the end with a store() and give it a `std::memory_order_release` before throwing it out of the window or `release`ing the pigeon so that the other thread may catch it. for write operations - `std::memory_order_acquire` doesnt make sense.

test_and_set() is a read-modify-write or a CAS (check and save) type operation, i.e., it does both a read and write and so it can be supplied any of the read or write type memory order, i imagine what we end up supplying to the test_and_set `std::memory_order_release` or `std::memory_order_acquire` (it can work with both and all other possible orders) is dependant on what we want to happen before this operation runs or what its surrounding operations are doing or how they are relying on this CAS operation.

3. (Recall / gotcha) The text says `std::atomic_flag` "can't even be used as a general Boolean flag." Precisely what operation is missing that makes this true, and what would go wrong if you tried to use `test_and_set()` itself as a stand-in for that missing operation?

Answer (med): the missing operation is a non-modifying read. test_and_set() will give u the past value which might be true or false and will make the flag true where as clear() will return nothing and make it false. we have no way of just checking without modifying what the current state of the flag is - test_and_set isn't non-modifying so it wont work.

### Build task: `spinlock_mutex`

Interface (signatures only):

```cpp
class spinlock_mutex {
public:
    spinlock_mutex();
    void lock();
    void unlock();
};
```

Thread-safety contract: mutual exclusion via busy-wait spin on a single `std::atomic_flag` — at most one thread may be between a successful `lock()` and its matching `unlock()` at any time. Must satisfy `BasicLockable` (i.e. works correctly as `std::lock_guard<spinlock_mutex>`). The book's Listing 5.1 uses `test_and_set(std::memory_order_acquire)` in `lock()` and `clear(std::memory_order_release)` in `unlock()` — fine to build to that exact spec even though 5.3 hasn't formally covered *why* acquire/release is what makes it correct yet (that's 5.3.6, explicitly deferred by the book itself); defaulting to `seq_cst` instead is also an acceptable, still-correct choice at this stage — just state which one you picked in the self-explanation below.

Named edge cases to watch:
1. **Genuine contention, not just "doesn't crash."** A driver that spins up many threads each doing `lock_guard`-protected increments of a plain (non-atomic) shared counter must show the *exact* expected final count, not just run without a sanitizer complaint — that's what actually proves mutual exclusion held, not merely that nothing visibly broke.
2. **Non-recursive by construction.** This spinlock is not reentrant — the same thread calling `lock()` twice without an intervening `unlock()` will spin forever against its own held flag. Don't build a test that exercises this (it'll hang); just be aware of it as a real, stated contract limitation, worth having ready if asked "does this deadlock under X."

**Self-explanation required before review** (2-4 sentences, closed-book): why does looping on `test_and_set()` actually guarantee mutual exclusion — specifically, what does the fact that "set the flag and read its old value" is *one indivisible operation* rule out that a naive "check a plain `bool`, then set it" (two separate steps) wouldn't? And what would concretely break if you built it that second, non-atomic way instead?

Answer: the busy wait design of using a while loop to block a thread is a classic way to keep a worker thread running on a task, what makes this pattern work specifically for the spin lock is using std::atomic_flag's test_and_set() method as the while condition which returns the last condition of the flag before setting it to true. so if a thread is spinning and blocked since the test_and_set() is returning true - meaning some other thread has the lock and then it returns false, this breaks the spinning of the while loop and the spinning thread can then proceed to carry out the commands below the spin_lock.lock() line since it now holds the lock and then when its done it can call spin_lock.unlock() to reset the flag to false and allowing some other thread to repeat the above process. the second way to build this described with a plain bool would require two operations, one to get the old value and the other to set the new value - thus opening us up to a race condition and making the plain bool unsuitable for making a literal mutex unless we use a std::mutex which defeats the purpose of implementing a spin lock mutex in the first place.

## Review — 5.2.2

Warm-up: correct and precise — blocking wait for the typically-idle worker (avoids wasting cycles), spin for the nanosecond-scale critical section (avoids the sleep/wake overhead dwarfing the actual work). Exactly the intended contrast, nothing to add.

Q1 — a real, substantive miss, not just imprecision: this reaches for the wrong C++11 guarantee. What's described (thread-safe local-static initialization — concurrent threads racing to initialize the *same* function-local static for the first time, made race-free since C++11) is genuinely a real thing, but it's 3.3.1's magic-statics mechanism, not what's being tested here. The guarantee this question is actually about is narrower and different: because `ATOMIC_FLAG_INIT` is a compile-time constant, a static-storage-duration `atomic_flag` gets *statically* (constant-)initialized — meaning there's no dynamic constructor call at program startup at all. That's what "no initialization-order issues" actually refers to: the classic *static/dynamic initialization order fiasco*, where two unrelated global/static objects in different translation units get constructed in an unspecified relative order, and one can end up being used before it's ready if it depends on the other. A statically-initialized `atomic_flag` sidesteps that whole problem — it's baked into the binary before `main()` even runs, before any ordering question between TUs is even relevant, and certainly before any thread could touch it. Worth having this distinction sharp: "thread-safe first-time init of one object" (magic statics) and "no cross-TU init-order dependency because there's no dynamic init at all" (this) are two different C++ safety guarantees that happen to both involve "static" and "C++11."

Q2 — right instinct, imprecise delivery. The acquire-reads/release-writes pairing (nice pigeon image) is correct, and the RMW-gets-everything reasoning for `test_and_set()` is exactly right. But the actual ask — name which of the six `memory_order_*` values are disallowed for `clear()` — never gets a clean, direct answer. Store operations (per 5.2.1's own established categorization) can only take `relaxed`, `release`, or `seq_cst` — meaning `acquire`, `consume`, *and* `acq_rel` are all disallowed for `clear()`, not just `acquire` (the only one actually named here). `consume` and `acq_rel` don't show up anywhere in the answer. Worth tightening to a direct list next time a question asks "name which values" — the intuition can carry the reasoning, but the precise enumerated answer still needs to land.

Q3 — correct and precise: the missing non-modifying query is exactly right, and the explanation of why `test_and_set()` can't stand in for it (it unconditionally sets the flag as a side effect of "checking," corrupting the very state you were trying to read) is exactly the mechanism. Nothing to add.

**Build (`spinlock_mutex`):** matches Listing 5.1 exactly — `flag(ATOMIC_FLAG_INIT)` in the member-initializer list (verified this compiles clean, `-Wall -Wextra` silent, on this toolchain), `test_and_set(memory_order_acquire)` spinning in `lock()`, `clear(memory_order_release)` in `unlock()`. Verified independently: the submitted driver's own three checks (wall-clock serialization time, exact final counter, sorted-interval no-overlap check) passed 5/5 plain and 10/10 ASan+UBSan runs — the sorted-interval-overlap check in particular is a genuinely sharp piece of test design, a direct timestamp-based proof of mutual exclusion that goes beyond just trusting the final count. I additionally ran an independent high-contention scratch stress test (20 threads × 20,000 increments each, no sleep, rapid lock/unlock cycling — a much harsher contention profile than the driver's own gentler 10-thread/300ms design) — 8/8 clean under ASan+UBSan, exact count every time. No bugs found anywhere. Edge case #2 (non-reentrant) correctly left undocumented-as-a-test and just noted in a comment, exactly per the spec.

Self-explanation: correct and complete — identifies the actual mechanism precisely (the indivisibility of "read old value + set to true" as one step is what prevents two threads from both observing `false` and both proceeding), and correctly diagnoses the concrete failure mode of the naive plain-`bool` alternative (the read-then-write split creates a real check-then-act race window).

**5.2.2 → passable, closed session 17.** Build is clean on first submission — no round 2 needed. Quiz: Q1 is a real miss worth remembering (static/constant-init avoiding the cross-TU init-order fiasco, not the magic-statics thread-safe-first-init mechanism from 3.3.1 — related but genuinely different guarantees); Q2 needs the precise value list tightened (acquire, consume, *and* acq_rel are all disallowed for a store, not just acquire); Q3 strong. Corrections given directly above, no redo required.

## 5.2.3 — Operations on std::atomic<bool>

```
$ python3 tools/review.py due   (2026-09-24)
Due now: 0 active (0 overdue) | maintenance due: 0 | due in next 3 days: 15 | total active: 50, retired: 1
WARM-UP: nothing due -> ask ONE cross-chapter design-judgment question
Open fix-list items: 8 (F1-F8)
LINT: uncommitted study-state changes (the 2026-09-24 migration)
```

Triage:
- Assignment returns a value, not a reference: already tested (5.2.1 Q4). Not re-quizzed.
- store / load / exchange categories: covered by queue item `5.2-memory-order-by-operation`. Not re-quizzed.
- What `expected` holds after a failed compare/exchange: **code-tracing** (Q1).
- Weak vs. strong, spurious failure, and when to prefer each: **recall** (Q2).
- The two memory-order arguments and the derived failure order: **recall** (Q3).
- The compare/exchange update loop ("new value depends on the current value"): a reusable mechanism the text describes in prose. **Build: `atomic_fetch_max`** (one sitting). Not also quizzed.

### Warm-up (nothing due: cross-chapter design judgment)

A trading system keeps a fee/limits table that a config thread replaces a few times a day. Eight strategy threads read it on every quote, and each read copies a handful of fields (well under a microsecond). The candidates are `std::mutex`, `std::shared_mutex` (3.3.2), and your `spinlock_mutex` (5.2.2). Which do you pick, and what concretely goes wrong with each of the other two under this access pattern? One condition: the box sometimes runs more busy threads than cores. How does that change the spinlock's behaviour?

Answer: the best candidate for protecting the table in the above described scenario is a std::shared_mutex, this is because of the description of its usage in the question, i.e., estimated eight concurrent reads might need to happen on the table at a time and the table updates only a few times a day. 

std::mutex is a bad choice since it will prohibit / serialize possible concurrent reads. std::mutex is a good choice for when reads and writes are proportional or roughly equivalent in number over the lifetime of the shared data structure.

spinlock_mutex is even worse because suppose - as per the question suppose we instantiate eight threads which are meant to read on a four core box, then we could end up with at least three cores constantly locked in a constant 100% while loop processor burn as the spin_lock will constantly check for a false coming from flag.test_and_set() to then acquire its own lock, and if we for some reason dont let go of the spin_lock for some reason like an error - then we might go into a deadlock though i think the all core lockup scenario is quite unlikely - however the all but one core busy wait situation is a near certain outcome of using spin_lock.

### Quiz

1. (Code-tracing) A colleague writes this to "claim" a flag so that only one thread proceeds:

```cpp
std::atomic<bool> claimed{false};

bool try_claim_forever() {
    bool expected = false;
    while (!claimed.compare_exchange_weak(expected, true)) { }
    return true;   // "I own it now"
}
```

Thread A has already claimed it (`claimed == true`). Thread B calls `try_claim_forever()`. Trace B's loop iteration by iteration: what is `expected` before and after each call, and what does B conclude? Then give two different fixes, one of which is the book's.

Answer (med): for thread B to stop blocking on the while loop of the try_claim_forever(), the while loop needs to be broken and for that the compare_exchange_weak() needs to return true. a compare_exchange_weak() will return false when it fails to write the desired value into the atomic its called upon, which can happen for _exchange_weak in two cases, one is a spurious fail - where LL and SC (two seperate operations in ARM and RISCV) are interrupted and in this case the expected is not updated with current value of the atomic, the second is a genuine fail to update value of atomic with expected caused due to mismatch in value of atomic var and expected. 

So with this all said, in B's case, in first run of the function, expected is false and atomic - claimed is true, so the expected value will be updated to true and the compare_exchange_weak() will return false which means we go on another iteration. in the next iteration, expected is true, and the atomic - claimed is false so claimed gets updated to true and that makes while - false and we break the loop thereby owning claimed. now there might be a case where another thread might interrupt the above described happy path since this is  compare_exchange_weak() so between Load-Linked  (LL) and Store-Conditional (SC) operations under the hood we might have a break in and in that it could make claimed as false instead of its original true value, so to ensure or check wether it was our thread's operation or some others that changed the value of claimed we need a secondary check like:

```cpp
std::atomic<bool> claimed{false};

bool try_claim_forever() {
    bool expected = false;
    while (!claimed.compare_exchange_weak(expected, true)
     && !expected) { }
    return true;   // "I own it now"
}
```
so now if we get false from the compare_exchange_weak() and we dont have an updated expected - we have a spurious fail, i.e., (true && true) and we dont break out of the loop. 
another way we could go is to use compare_exchange_strong() instead which would not have risk of spurious fails and not need us to confirm `expected` mutation to determine if its a spurious fail or not, so we could have:

```cpp
std::atomic<bool> claimed{false};

bool try_claim_forever() {
    bool expected = false;
    while (!claimed.compare_exchange_strong(expected, true)) { }
    return true;   // "I own it now"
}
```
and this would block us from claiming with no problem till our thread B was actually the one that successfully ran the mutation.

**Correction (2026-09-26):**
The real bug in this code from the question:

```cpp
std::atomic<bool> claimed{false};

bool try_claim_forever() {
    bool expected = false;
    while (!claimed.compare_exchange_weak(expected, true)) { }
    return true;   // "I own it now"
}
```
We know thread A has a lock - but then when we run the same code above from B, if iteration 1 has expected as false and the atomic was set by A to true before returning true, then by iteration 2 in B, expected will have been set to true and claimed set to true again by B as well without having the need of thread A to have set the claimed be set to false before B sets claimed to true, so basically both will have set claimed to true.

what ought to happen is that only one thread should have set claimed as true and returned true from this function and the others ought to block until the one that set true sets it back to false so that they can set claimed to true and return. so for that to happen, we need to change the code a bit so that they keep spinning until they are the ones that set claimed to be true before returning, and they ought not return before that. So we should write:

```cpp
std::atomic<bool> claimed{false};

bool try_claim_forever() {
    bool expected = false;
    while (!claimed.compare_exchange_weak(expected, true)) {
      expected = false;
    }
    return true;   // "I own it now"
}
```
this way on each retry, expected is false and unless claimed is also false, we keep on looping.


2. (Recall) What is a *spurious failure* of `compare_exchange_weak()`, and what property of some hardware makes it possible? `compare_exchange_strong()` doesn't fail spuriously, so why doesn't everyone use it inside retry loops? Give the book's rule for when strong is the better choice even inside a loop.

Answer (med): A *spurious failure* of `compare_exchange_weak()` is caused when on some CPU architectures like ARM and RISCV - there is no single compare-and-save CPU instruction, and instead there are a pair of instructions, i.e., LL or Load-Linked (read with a watch on that address) and SC or Store-Conditional (write the value if no other thread acted on that address). an a heavily loaded system, these two can be interrupted by another thread such that it could acquire the core between LL and SC by the OS scheduler and this leads to a spurious fail which means that expected is not updated with the current value and the atomic variable is not updated with the desired value and the compare_exchange_weak() returns false.

on such architectures, we could avoid spurious fails by using `compare_exchange_strong()` which only returns false if there is mismatch between the atomic var and expected values and will also update the expected value; however since we know that there isnt one atomic CPU command to do this - there must be use on internal mutex to achieve this without interruption. this ofcourse means more overhead than `compare_exchange_weak()`. the books suggests that:

One should use compare_exchange_WEAK when:
 - You are already in a retry loop for other reasons (e.g. updating a value that depends on the current value)
 - The body of the loop is cheap to repeat
 - You want maximum performance on ARM/RISC hardware

Use compare_exchange_STRONG when:
 - You just want to know "did I win the race or did another thread?" and don't want to write a loop to force a write.
 - The calculation of 'desired' is expensive to repeat, i.e., with each cycle - perhaps the desired value needs to change based on variables being set elsewhere by other threads and that could mean we have a function call in the desired value place and that is expensive to do repeatedly in a retry loop we have to use with _WEAK.

**Correction (2026-09-26):**
The reason I gave for more overhead of compare_exchange_strong vs compare_exchange_weak was  wrong, i.e., there isnt a internal mutex in _strong, that wraps the LL and SC operations, there is a retry loop around them, so the real cost or overhead comes not from a mutex but from a retry loop within our own retry loop, i.e., a nested while loop - the compare_exchange_weak essentially allows for our own external retry loop to handle all failures.

3. (Recall) Compare/exchange can take two memory orders. Which values are forbidden as the *failure* ordering, and why? Give the second restriction the text states about failure vs. success. If you pass only a success ordering, what failure ordering do you get for `memory_order_release`, `memory_order_acq_rel`, and `memory_order_seq_cst`?

Answer (med): so failure case is when the write part of the CAS or compare/exchange operation was unsuccessful, this means that the failure memory order cannot have a write type order as it wouldnt make sense as a write didnt happen. this means we can put std::memory_order_release there. the rule is that the success order can have read_write or write memory order but failure case cannot have a write order and also, failure case cannot have a memory order thats on par or higher in authority than the success case memory order. So, if we put something like a `memory_order_acq_rel` with both read and write orders for the success order and dont give the failure case order, the memory order we used for the success case will get stripped of the write part, i.e., `memory_order_acq_rel` becomes `memory_order_acquire` and gets provided as a the failure case memory order. One exception is the `memory_order_seq_cst` where if we give this for success, its also given for the failure case and if we dont give any memory order - success or failure - then `memory_order_seq_cst` gets placed for both cases.

**Correction (2026-09-26):**

Ok so the wording of the prevous answer needs some tightening. In a failed CAS, the read does go through but the write operations doesnt go through and so what we have is a general rule that answers the first part of the question, i.e., the failure memory order cannot have a write type order, this means the failure order can't be release or acq_rel, because a failed CAS performs no store. the second restriction is that failure case cannot have a memory order thats higher in authority than the success case. If we pass only a success case ordering then the derived orders for failed case are - release -> relaxed, acq_rel -> acquire, and everything else remains unchanged.

Confidence-tag each answer (low/med/high).

### Build task: `atomic_fetch_max`

Interface (signature only):

```cpp
template <typename T>
T fetch_max(std::atomic<T>& target, T value);
```

**Contract:**
- Atomically replaces the value with `max(old, value)` and returns `old`, the value held immediately before the operation (same convention as `fetch_add`).
- Linearizable: concurrent calls behave as though they ran one at a time in some order. The stored value never decreases.
- No mutex. Lock-free whenever `std::atomic<T>` is.
- Default (`seq_cst`) ordering throughout. No memory-order parameters yet (5.3).

**Named edge cases:**
1. **`value <= current`: don't store.** When the value isn't a new maximum, return without writing to `target`. Most calls in a feed-arbitration workload are losers, and a write would needlessly invalidate the cache line on every other core. You must still return a value that was actually held by `target`. Be ready to say which operation is the linearization point on this no-store path.
2. **Lost updates under contention.** The driver must prove that no update is lost, not just that nothing crashed:
   - The final value equals the global maximum of everything submitted, with many threads submitting interleaved values.
   - Within one thread, successive returned `old` values never decrease.
   - An observer thread polling `load()` never sees the value go down.
3. **A/B arbitration check.** Two "feed" threads each call `fetch_max(seq, s)` for `s = 1..N` in order. A feed wins `s` iff the returned `old < s`. Assert that every `s` is won by exactly one feed, and that the two feeds' win counts sum to `N`. Before you run it, work out why this invariant must hold.

Choosing weak vs. strong is up to you; justify it below.

**Self-explanation required before review** (2–4 sentences, closed-book): Why can't concurrent `fetch_max` calls lose an update, when a plain `auto cur = target.load(); if (value > cur) target.store(value);` can? Give an interleaving that breaks the load-then-store version. Which compare/exchange variant did you use, and why is it the right one here?

Answer: concurrent fetch_max calls cannot lose an update with a larger because we have a while loop which breaks when the current value in the atomic variable supplied is no longer less than the supplied value. in a simple if statements flow, ensuring that the replacement is made wouldnt be possible as comparison could fail even if current value in the atomic was less than the supplied `value` beacuse another thread could have raced ahead and replaced the current value with a larger value which was still smaller than the supplied value of the other thread yet not the value it expected, causing it to fail - the only way to ensure it retries with the correct expected value is to use a while loop. I used a compare_exchange_strong() function in the while loop because i checked my PC architecture and it was an x86 architecture, not ARM or anything else and some searches allowed me to surmise that there is no efficiency or speed gain or benefit in using compare_exchange_weak and then having to make a more complex condition for proper retries.

## Review: 5.2.3

### Warm-up

**`5.2-read-mostly-lock-choice` -> miss** (new item; the oversubscription part was the probed point).

> "the best candidate for protecting the table in the above described scenario is a std::shared_mutex"
> "std::mutex is a bad choice since it will prohibit / serialize possible concurrent reads."

Both correct.

> "we could end up with at least three cores constantly locked in a constant 100% while loop processor burn"

That's true of any contended spinlock. It doesn't answer what *changes* when threads outnumber cores. The oversubscription answer is **lock-holder preemption**:
- The OS can deschedule the holder in the middle of its sub-microsecond critical section.
- Every waiter then spins through its whole time slice (milliseconds) waiting for a thread that isn't running. Because the waiters are occupying the cores, the holder takes even longer to get rescheduled.
- The result: a 1 µs hold turns into a multi-millisecond stall.
- `std::mutex` waiters sleep in the kernel (futex) and hand the core back to the holder.

Two smaller notes:
- "if we don't let go of the spin_lock ... like an error ... deadlock" applies to any lock. `lock_guard` handles it. It isn't specific to spinlocks.
- On the pick itself: `shared_mutex` is a defensible answer here. For sub-microsecond reads, though, each reader's lock and unlock is still an atomic RMW on one shared counter, so 8 readers bounce one cache line. It's often no faster than a plain mutex. For a table that's replaced rarely, the usual design is an immutable snapshot swapped by pointer, with no reader-side lock at all. That's ahead in 5.2.4 and ch7.

### Quiz

**Q1 -> miss** (confidence: med)

> "in the next iteration, expected is true, and the atomic - claimed is false so claimed gets updated to true and that makes while - false and we break the loop thereby owning claimed."

`claimed` is not false. A still holds it, so it's still `true`. The actual trace:

| iter | `expected` before | `claimed` | CAS result | `expected` after |
|---|---|---|---|---|
| 1 | false | true | fails (false != true) | **true** (current value written back) |
| 2 | true | true | **succeeds** (true == true), stores true | true |

B exits the loop and returns "I own it" while A also owns it. The failed call in iteration 1 set up iteration 2 to "succeed" against the held flag. That's the bug: **`expected` is never reset**. It has nothing to do with spurious failure.

Neither fix works:
- `&& !expected` with an unconditional `return true;`: after the genuine failure, `expected == true`, so the loop exits and B still returns true. The book's loop needs its result reported: `return !expected;` (true only if B stored the value).
- `compare_exchange_strong` without resetting `expected`: the same two-iteration trace. B wins trivially in iteration 2.

Correct fixes, depending on the intended semantics:
- **Try once:** the book's loop plus `return !expected;`, or `bool e = false; return claimed.compare_exchange_strong(e, true);`.
- **Wait until it's free:** reset inside the loop, `while (!claimed.compare_exchange_weak(expected, true)) expected = false;`. Equivalently, `while (claimed.exchange(true)) {}`, which is the atomic_flag spinlock written with `atomic<bool>`.

**Q2 -> miss** (confidence: med). The "why not always strong" part rests on a false belief.

> "A *spurious failure* ... there are a pair of instructions, i.e., LL ... and SC ..."

Correct. The rule for strong ("the calculation of 'desired' is expensive to repeat") is also correct.

> "since we know that there isnt one atomic CPU command to do this - there must be use on internal mutex to achieve this"

This is wrong. On LL/SC hardware, `compare_exchange_strong` is **a retry loop around LL/SC**. It retries on a lost reservation and returns false only when the values really differ. The text says strong "contains a loop". It is still lock-free, and no mutex is involved. That loop is the reason for the book's advice:
- Strong inside your own retry loop is a **loop inside a loop**.
- Weak lets your loop do the retrying.
- The cost difference is the nested loop, not a lock.

Precision nit, not graded: on a spurious failure, `expected` *is* written with the current value, which happens to equal what was already in it.

**Q3 -> miss** (confidence: med)

> "the failure memory order cannot have a write type order ... this means we can put std::memory_order_release there."

I'm reading "can" as a typo for "can't". Even so, `acq_rel` is never named as forbidden.

> "failure case cannot have a memory order thats on par or higher in authority than the success case"

"On par" is wrong. Equal is allowed: `seq_cst`/`seq_cst` and `acquire`/`acquire` are both fine. The text says only "stricter" is forbidden.

On the derived orders: `acq_rel -> acquire` and `seq_cst -> seq_cst` are both right. `release -> relaxed` is never stated, and that was one of the three the question asked for.

The complete answer:
- The failure order can't be `release` or `acq_rel`, because a failed CAS performs no store.
- Derived orders: `release -> relaxed`, `acq_rel -> acquire`, everything else unchanged.

Standard update (I checked; the book states the C++11/14 rule): **C++17 removed the "failure no stronger than success" restriction** (P0418R2). So `compare_exchange_weak(e, d, memory_order_relaxed, memory_order_acquire)` is legal in this repo's C++17. The `release`/`acq_rel` ban on the failure order still applies.

### Build

```
atomic_fetch_max: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK
```

**Library: correct. No bug found.**
- The loop is load, then `while (old < value)` with a strong CAS. A failed CAS refreshes `old`, so the condition is re-evaluated against the true current value.
- It returns the pre-operation value on both paths.
- It never stores when `value <= current`.
- Linearization points:
  - Success path: the successful CAS.
  - No-store path: the load, or the failed CAS, that last produced `old >= value`.

Findings in the library, none of them correctness issues:
1. **The `int& win_count` overload is test instrumentation inside the reusable header**, and it duplicates the function body. It's the same problem as F6. It isn't needed, because the return value already carries the information: `old < value` exactly when this call stored. Derive wins in the driver.
2. **Weak vs. strong:**
   - Strong is correct, and on x86 both compile to `lock cmpxchg`.
   - The reason you gave, that weak "needs a more complex condition for proper retries", is wrong for this loop. On a spurious failure `old` is unchanged, `old < value` is still true, and the loop just retries. Weak drops in with no other change.
   - That's the book's case for weak: `desired` is cheap to compute and you're already looping. The `&& !expected` condition is specific to the bool claim pattern.
3. Nits:
   - The spec signature was `fetch_max`, and the header uses `fetchMax`.
   - The `// race` comments mark interleaving points, not races. Nothing here is a data race, so the label is misleading.
   - `<thread>` is included but unused.
   - `fetchMax(std::atomic<long>&, 5)` fails template deduction. The standard library's approach is to take `typename std::atomic<T>::value_type`. Optional.

**Driver: doesn't prove the contract.** I tested it against a mutation: a deliberately broken load-then-store `fetchMax`, run in scratch with your unmodified driver.
- **54/100 runs caught it.** In the 60 runs where I checked which assert fired, all 26 failures came from Test 3's sum assert.
- **Tests 1 and 2 caught 0.**

Per test:
- **Test 1** can't detect a lost update:
  - The maximum, 90, comes only from thread 9's last few calls.
  - A lost update would need a slower thread to overwrite 90 afterward, and in practice that never happens.
  - The returned `old` is thrown away, so two of the spec's three edge-case-2 checks are missing: per-thread `old` non-decreasing, and the observer thread.
- **Test 2** is single-threaded. It's fine as a no-store smoke test. `x` is unused.
- **Test 3:**
  - `sleep_for(10ms)` per iteration throttles contention and costs about 1 s per run. It still catches some failures, because both threads wake together.
  - It asserts only the sum, not "each `s` won by exactly one feed". The sum happens to be equivalent here, because nobody can ever win zero times. You'd have to prove that, though, and the per-`s` check tells you *which* `s` failed.
  - The assert message says 99 but the code checks 100. The code is right, since `s = 0` never wins.
  - It uses the instrumentation overload instead of the return value.
- `std::accumulate` needs `<numeric>`. It compiles only through a transitive include in libstdc++ 11.
- `-Wall` flags 3 unused variables.

**Independent stress test** (scratch, your library unmodified):
- Phase 1:
  - 8 threads × 200k values, ramped and interleaved so that most calls actually store.
  - Checks: per-thread `old` non-decreasing, an observer asserting `load()` never decreases, and final value == global max.
- Phase 2: A/B with 500k sequence numbers and no sleeps. Winners are decided by the return value, with an exactly-one-winner assert per `s`.
- **Your library: 30/30 clean at -O2, 5/5 TSAN clean.** A/B split in one run: 337766/162234.
- **The broken load-then-store version: 29/30 caught.** Phase 1 alone: 18/20, all through "per-thread old decreased".
- Lesson for your fix: my first version drew uniform random values and caught **0/20**. The maximum plateaus quickly, so almost every call takes the no-store path and nothing can be lost. Test values must keep forcing stores.

**Self-explanation:**

> "concurrent fetch_max calls cannot lose an update ... because we have a while loop"

That's the wrong mechanism.
- The loop gives *retry* (progress).
- *Safety* comes from the CAS: the store commits only if `target` still equals the `old` you based the decision on, checked and written as one indivisible step. A stale decision can't be committed.
- The interleaving the prompt asked for isn't given:
  1. `target = 5`.
  2. T1 loads 5 (value 9). T2 loads 5 (value 7).
  3. T1 stores 9. T2 stores 7.
  4. The 9 is lost and the maximum went *down*.
- The x86 justification for strong is sound. The claim that weak needs a more complex condition is not (see finding 2 above).

**Status:** the library is verified. The driver fix is **F9**. The build isn't done until F9 is re-verified.

### Queue updates

```
python3 tools/review.py add 5.2 5.2-read-mostly-lock-choice "Read-mostly table, sub-us reads, rare replacement: mutex vs shared_mutex vs spinlock; what goes wrong with each, and specifically what lock-holder preemption does to a spinlock when runnable threads > cores." --kind miss
python3 tools/review.py add 5.2 5.2-cas-expected-overwrite "After a failed compare_exchange, expected holds the current value. Trace a claim loop that never resets expected against an already-held flag, and give correct try-once and wait-until-free versions." --kind miss
python3 tools/review.py add 5.2 5.2-cas-weak-vs-strong "What a spurious failure of compare_exchange_weak is (LL/SC); how strong is implemented on LL/SC hardware (retry loop, still lock-free, NOT a mutex); why strong inside a retry loop is a double loop; when strong is still the better choice." --kind misconception
python3 tools/review.py add 5.2 5.2-cas-failure-ordering "compare_exchange failure ordering: which values are forbidden and why; derived failure order when only success is given (release->relaxed, acq_rel->acquire, else same); what C++17 changed (stronger-than-success now allowed)." --kind miss
python3 tools/review.py add 5.2 5.2-cas-update-loop "Why a CAS loop (fetch_max) can't lose an update where load-then-store can: give the interleaving; what the loop contributes vs what the CAS contributes; linearization point of the no-store path; why weak drops in unchanged." --kind miss
python3 tools/review.py add 5.2 5.2-atomic-bool-vs-flag "What std::atomic<bool> adds over std::atomic_flag (construct/assign from bool, load, store, exchange, compare/exchange) and what it gives up (no lock-free guarantee)." --kind core
```

### Fix list

- **Opened F9:** `atomic_fetch_max` driver and instrumentation cleanup.
- Closed: none.

### Regrade (2026-09-25, after the pacing policy change)

The framework now grades only material the book covers (Book-only rule). Applied to this review:
- **Warm-up -> hit.** The book-answerable parts (`shared_mutex` fits; `std::mutex` serializes readers; the spinlock serializes them and burns CPU) were all correct. The oversubscription part needed lock-holder preemption, which the text doesn't cover; it moves to Beyond the book below. Item `5.2-read-mostly-lock-choice` is now `core`.
- **Q1, Q2, Q3 stay misses.** All three were answerable from the text; for Q2 the text says strong "contains a loop". Q3's C++17 note is Beyond the book, not graded.
- **Build: F9 narrowed** to the one gap in a test the spec asked for: Test 1's values stop rising, so it can't detect a lost update. The other driver findings above are notes. (The spec will list "Tests to write" in plain English from now on.)

### Beyond the book (ungraded)

- **Lock-holder preemption:** with more runnable threads than cores, the OS can pause a spinlock's holder mid-section, and every waiter spins through its time slice for nothing.
- **Signal handlers and lock-free atomics:** guard with `static_assert(std::atomic<bool>::is_always_lock_free)`.
- **C++17 and compare/exchange orderings:** the "failure no stronger than success" restriction was dropped (P0418R2); `release`/`acq_rel` are still forbidden as the failure order.
- **Hardware detail:** machines without a single compare-and-exchange instruction typically use a load-linked/store-conditional pair; strong wraps it in a retry loop.

### Correction check (2026-09-26)

Corrections are written after seeing the review, so they aren't graded as retrieval and the queue is unchanged. The items are retested in warm-ups on their due dates.

**Q1: diagnosis right, code still has the bug.**

> "by iteration 2 in B, expected will have been set to true and claimed set to true again by B ... so basically both will have set claimed to true."

This is correct: after the failed first call, `expected` is `true`, so the second call "succeeds" against A's held flag. The intent is also right ("keep spinning until they are the ones that set claimed to be true").

The corrected loop keeps `&& !expected`, though:

```cpp
while (!claimed.compare_exchange_weak(expected, true) && !expected) { expected = false; }
```

After the genuine failure in iteration 1, `expected == true`, so `&& !expected` is false and the loop **exits before the body can reset `expected`**. The function then returns `true`. The reset only runs after a spurious failure, when `expected` is already `false`.

I compiled it in scratch with `claimed = true` beforehand, and it printed "B returned true while claimed is still held by A". Deleting `&& !expected` gives the wait-until-free version from the review: `while (!claimed.compare_exchange_weak(expected, true)) expected = false;`.

The rule of thumb: `&& !expected` means "stop as soon as someone else has it". It belongs to the *try-once* pattern (followed by `return !expected;`), never to a wait loop.

**Q2: correct.** "there is a retry loop around them ... a retry loop within our own retry loop, i.e., a nested while loop" is exactly the book's point.

**Q3: correct and complete.**
- It names `release` and `acq_rel` as the forbidden failure orders, and why ("a failed CAS performs no store").
- It gives the restriction as "not higher than" success, with "on par" gone.
- It gives all three derived orders: release → relaxed, acq_rel → acquire, the rest unchanged.

**Build (F9):** still open in the ledger.

## 5.2.4–5.2.5 — Operations on std::atomic<T*> and on the standard atomic integral types

```
$ python3 tools/review.py due   (2026-09-27)
Due now: 21 active (14 overdue) | maintenance due: 0 | due in next 3 days: 15 | total active: 56, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.2-cas-weak-vs-strong  (1d overdue)
  2. [miss] 2.3-scoped-thread-exclusive-ownership  (2d overdue)
  3. [miss] 4.1-notify-outside-lock  (2d overdue)
Also due: 18 more (see due output)
Open fix-list items: 9 (F1-F9)
Lint: OK
```

Triage:
- Not copyable; constructible/assignable from `T*`; `is_lock_free`, `load`, `store`, `exchange`, compare/exchange "same as atomic<bool>": already tested (5.2.1 Q3, 5.2.3). Not re-quizzed.
- Named functions return the old value, operators return the new value, by value: tested as a concept in 5.2.1 Q4. Re-tested here only where it combines with something new (pointer arithmetic in Q1).
- Pointer arithmetic in units of `T`, plus old-vs-new return on `atomic<T*>`: **code-tracing** (Q1).
- Operator forms are always `seq_cst`; the named form takes an ordering (and, being RMW, any ordering): **recall** (Q2).
- The integral operation set, what's missing, and why that's rarely felt: **recall** (Q3).
- "Missing operations can be built with a compare/exchange loop": that mechanism is the `atomic_fetch_max` build (F9 open) and queue item `5.2-cas-update-loop`. **No new build.**

### Warm-up

1. A loop recomputes its desired value from `expected` on every pass: `while (!x.compare_exchange_???(expected, f(expected))) {}`. On a machine that has no single compare-and-exchange instruction, how is `compare_exchange_strong` built, and what can the weak form do that strong never does? For this loop, when do you pick weak and when strong, and what does the wrong pick cost you in each direction?

Answer (med): On a machine that does not have a single compare-exchange instruction, compare_exchange_strong is built using an internal retry or while loop, i.e., the internal retry loop for compare_exchange_strong keeps on retrying until we get an actual mismatch between expected and current or we get a success match but if the write fails - the internal loop keeps retrying with the same expected and desired values supplied by the compare_exchange_strong() command - this leads into some of the answer of the question, if we use _strong, then for spurious fails, we need not recall f(expected) since its just gonna retry with the one value it got from the call in the internal retry loop; in contrast to this, compare_exchange_weak() will need us to handle spurious fail retry as well in the one external retry loop and this means that in the case where we have a lot of spurious fails, we also have a lot of f(expected) calls for each retry. So _strong doesnt need us to handle spurious fails and does that itself using an internal retry loop which in its own passes will not recalculate f(expected) in its nested retry setup; the _weak does need us to handle spurious fails due to other threads interrupting in between LL and SC commands and will thus require more f(expected) calls in each retry of the single retry loop. the cost is subjective in either case - we have two variables to consider, the task load which translates to the rate of interruption each thread is expected to see during CAS instructions and we also have the other side which is how expensive is the call f(expected) for each pass. if the f(expected) call is expensive, then we should use _strong() even in high load to avoid having to make unnecessary calls of f(expected) even if this causes a nested loop and more overhead and blocking of the core. on the other hand, if f(expected) is relatively inexpensive, then the right choice is to use _weak() to avoid the overhead from nested retry loops we see in _strong() calls. the wrong choice leads to more blocking, CPU burn and unnecessary overhead.

2. A teammate adds `std::thread& get()` to `scoped_thread` "for debugging". Give a concrete sequence of calls through that accessor that makes `scoped_thread`'s destructor misbehave, and say what happens when it does. What single invariant do the class's other two choices (the constructor's check, and having no default constructor) also exist to protect?

Answer (med): the whole point of the scoped_thread class is "No external references should be available to caller scope so as to avoid .join() calls to the internal thread object by anyone but the class's own methods.". adding a method `std::thread& get()` to `scoped_thread` is a direct violation of this concept which is that basis of the class and is bound to create more bugs than it solves. furthermore if suppose we take the implementation we have of scoped thread here in `ch02_managing_threads/scoped_thread/include/scoped_thread.hpp` and we call `std::thread& t2 =  sct.get()` and then call `t2.join()` we wont get an error from the destructor when it does get called since we have the guard clause for this in the destructor `if (t.joinable()) {t.join();}`; that being said, the books definition has no guard clause and its destructor is straight like this `~scoped_thread(){t.join();}` this is because the constructor already has the joinable check and after it moves the underlying rvalue into the internal thread object, then it doesn't expect an external reference to it and so if an external reference were to call join() and then the destructor was to call join() then we would get a `std::system_error`. the class has no copy constructor or copy assignment constructor and the whole class is built over one invariant, there will only be one reference to the underlying system thread rvalue and that is the internal std::thread object and copy constructors dont support that idea however move constructors are based on this idea so that class in `ch02_managing_threads/scoped_thread/include/scoped_thread.hpp` does have those defined to ensure the single reference invariant is always respected. 

3. Producer and consumer share a queue `q`, mutex `m`, and condition variable `cv`:

```cpp
// producer
{ std::lock_guard<std::mutex> lk(m); q.push(x); }
cv.notify_one();                       // after the lock is released

// consumer
std::unique_lock<std::mutex> lk(m);
cv.wait(lk, [&]{ return !q.empty(); });
```

Is there any interleaving where the consumer ends up asleep with an item sitting in `q`? Argue it from what `wait(lk, pred)` checks, and which lock is held when it checks.

Answer (med): yes there is one scenario that comes to mind in which, if the producer thread was called before the consumer thread, i.e., the producer code running producer thread, loads the queue with an item and hits a notify_once() however, the other thread hasnt run the consumer code yet, then that notify_one event gets missed and on starting the consumer thread late it sleeps waiting for the next push notification, oblivious to the item already inside the queue, the predicate gets checked for spurious wakes and for notifications, if the above situation happens and there is a notify_one and one consumer thread, then we definitely end up with the consumer ends up asleep with an item sitting in `q`.

### Quiz

1. (Code-tracing) Worker threads claim slots in a preallocated ring:

```cpp
struct Order { char data[64]; };        // sizeof(Order) == 64
Order ring[4];
std::atomic<Order*> next{ring};

Order* claim_a() { return next++; }
Order* claim_b() { return next += 1; }
Order* claim_c() { return next.fetch_add(1); }
```

   (a) Starting fresh, three threads each call `claim_a()` once. Which slots (`ring[i]`) do they get, and afterwards what does `next` point to, as an index and as a byte offset from `ring`?
   (b) Starting fresh again, same thing with `claim_b()`. Which slots do they get, and what goes wrong with this version once the ring fills up?
   (c) Does `claim_c()` behave like `claim_a()` or like `claim_b()`?

Answer (med): 

(a) next++ is post increment so it will return old value and then increment, so if threads A,B,C call claim_a(), then atomic operation means each calls the post increment in succession to another or one after the other basically. so assuming for demonstration sake, they call next++ in A, B, C order as well, then A gets the pointer to ring[0], B gets ring[1] and C gets ring[2]. with that said, after C's call the next sits pointing to ring[3]'s pointer and in terms of byte offset next is (64*3) 192 bytes to the right of next[0].

(b) next += 1 will add 1 first and return the new value, its converse for getting old value is fetch_add(). so if threads A,B,C call claim_b()'s next+=1 line in that order then they are left with pointers to ring[1], ring[2] and ring[3] respectively. Its a bit unclear what the question means when it says what goes wrong when the ring fills up, if it means calling claim_b again then the failure mode is whats commonly called a one-past-the-end pointer (or past-the-end pointer). It's valid to create and compare that pointer, but you cannot dereference it since it contains garbage that was never part of the array. this is a problem with using the built in c style arrays and is not a problem when using vectors since they maintain element count and state however we do then have to worry about the extra functionality and head based operation of the std::vector object and not to mention making it thread safe against data races - at least the methods we need in the new thread safe interface so there is that drawback.

(c) claim_c() behave like claim_a() because as i already mentioned next += 1 will do the increment first and return the new value where as fetch_add() and next++ will return the old value first and then do the increment.

2. (Recall) Code has `head += 2;` on a `std::atomic<Node*>`, and a reviewer wants it to use `memory_order_release`.
   (a) What memory ordering does `head += 2` have now, and why can't you add an ordering to that line?
   (b) Rewrite it so it uses release ordering. Your rewrite returns a different value from `head += 2`: which one, and what has to change if the caller was using the result?

Answer (med):

(a) the memory order on head+=2 is std::memory_order_seq_cst which is the default ordering on atomic ops. there is no syntacticaly valid place to put another memory order on operator forms like -= or ++ or += and others like these, there literally is no place for another memory order so if one is desired, better to use a named variant, in this case - fetch_add().

(b) for head += 2, the caller will get the new value, if we are to use std::memory_order_release, then we need to use head.`fetch_add(2, std::memory_order_release)` and this will return the old value of head to the caller. if the caller was using the result and the caller needs the new value then we ought to use an additional load() operation next to this addition operation.

3. (Recall) A `std::atomic<unsigned>` holds a bitmask of which venues are currently connected.
   (a) Which compound-assignment operators does a plain `unsigned` support that `std::atomic<unsigned>` doesn't?
   (b) Which named `fetch_` operations does `std::atomic<unsigned>` have that `std::atomic<Order*>` doesn't?
   (c) Why does the book say the gap in (a) is rarely a noticeable loss?

Answer (med):

(a) the missing operations for a `std::atomic<unsigned>` in comparison to `unsigned` are `*, /, %, <<, >>`.
(b) the missing fetch operations for `std::atomic<Order*>` are `fetch_or()` and `fetch_and()`.
(c) the missing operations in a like *, <<, >>, / are almost never needed for atomic integers because atomic integers are often needed for bit masks and counters which dont need such operations for their function. if such operations are needed then they can be implemented using combination of compare_exchange_?? operations and retry loops.

Confidence-tag each answer (low/med/high).

### Beyond the book (optional, ungraded; not queued unless you say "queue it")

On x86, `fetch_add` compiles to one `lock xadd`, which returns the old value. There's no x86 instruction that does an atomic OR *and* returns the old value. So what do you expect the compiler to emit for `mask.fetch_or(bit)` when the returned value is used, versus when it's ignored? (Paste both into Compiler Explorer at `-O2` to check, if you like.)

I looked at the machine code and didnt get any of it but the key difference:

Return value used: compiler needs a CAS loop (lock cmpxchg) because it must obtain the old value.

Return value ignored: compiler can use the simpler atomic lock or instruction.

So the key idea is: not needing the old value lets the compiler generate a cheaper atomic operation.
## Review: 5.2.4–5.2.5

### Warm-up

- **`5.2-cas-weak-vs-strong` -> hit.**
  > "compare_exchange_strong is built using an internal retry or while loop"
  > "if the f(expected) call is expensive, then we should use _strong() ... if f(expected) is relatively inexpensive, then the right choice is to use _weak()"

  No mutex this time, and the choice rule matches the book. One wording nit: strong doesn't "block" the core. It's a retry loop and still lock-free, so the cost is the extra looping, not blocking.

- **`2.3-scoped-thread-exclusive-ownership` -> hit.**
  > "if an external reference were to call join() and then the destructor was to call join() then we would get a `std::system_error`"
  > "there will only be one reference to the underlying system thread ... and that is the internal std::thread object"

  The accessor breaking the book's unconditional destructor join is the probed point. Two notes:
  - The full invariant is "it *always* owns exactly one *joinable* thread, and only it can touch that thread". The constructor check and the missing default constructor provide the "always joinable" half: no `scoped_thread` can ever be empty. The answer went to copy/move instead, which wasn't asked.
  - With *your* guarded destructor, the accessor fails silently rather than loudly: `sct.get().detach();` means the destructor skips the join, and the thread outlives the scope. That's the 2.1 dangling-reference-to-locals problem, which is worse than a crash. "we wont get an error" is true, but it isn't a safe outcome.

- **`4.1-notify-outside-lock` -> miss.** The item was upgraded to `misconception`.
  > "the producer ... hits a notify_one() however, the other thread hasnt run the consumer code yet, then that notify_one event gets missed and on starting the consumer thread late it sleeps waiting for the next push notification, oblivious to the item already inside the queue"
  > "the predicate gets checked for spurious wakes and for notifications"

  This is the same belief as the original 4.1 Q7. `wait(lk, pred)` checks `pred` **first**, with `m` held, and sleeps only if it's false. The book (4.1.1) says `wait()` "checks the condition ... and returns if it's satisfied". Both orders are safe:
  - **Producer first:** the item is already in `q` when the consumer arrives. `pred` is true, so the consumer never sleeps. A notify with nobody waiting is harmless, because the state lives in `q`, not in the notification.
  - **Consumer first:** it checks `pred` (false) with `m` held, and `wait` unlocks `m` and starts waiting as one step. The producer can't push until it gets `m`, so by the time it pushes and notifies, the consumer is already waiting and gets woken.

  Notifying after the unlock only means the woken consumer doesn't immediately block on a mutex the producer still holds. The book's own Listing 4.1 does exactly this. The lost-wakeup bug needs a wait **without** a predicate (or a flag checked outside the lock).

### Quiz

**Q1 -> hit** (confidence: med)
> "A gets the pointer to ring[0], B gets ring[1] and C gets ring[2] ... next is (64*3) 192 bytes to the right"
> "A,B,C ... are left with pointers to ring[1], ring[2] and ring[3]"
> "claim_c() behave like claim_a()"

All three parts are correct. On (b):
- The first failure is that **`ring[0]` is never handed out**. With 4 slots, the 4th caller already gets `ring + 4` (past the end), even though a slot is unused. `claim_a` also runs off the end, but only on the 5th call.
- Nit: the past-the-end pointer doesn't "contain garbage". It's a valid address to form and compare, but it doesn't point at an element, so dereferencing it is undefined. The `std::vector` tangent doesn't help here, because the problem is the missing bound check, and a vector would need one too.

**Q2 -> miss** (confidence: med)
> "there literally is no place for another memory order ... better to use a named variant, in this case - fetch_add()"
> "`fetch_add(2, std::memory_order_release)` and this will return the old value"

Both correct.

> "if the caller needs the new value then we ought to use an additional load() operation next to this addition operation"

This is the race the return-value design exists to prevent (5.2.1 Q4, and the text here: the return value lets "the calling code ... perform actions based on what the previous value was"). Between your `fetch_add` and the `load()`, another thread can move `head` again, so the load returns *its* result, not yours. The correct version computes the new value locally: `Node* now = head.fetch_add(2, std::memory_order_release) + 2;`. Queued as `5.2-rmw-result-not-reload`.

**Q3 -> hit** (confidence: med)
> "`*, /, %, <<, >>`"
> "`fetch_or()` and `fetch_and()`"
> "atomic integers are often needed for bit masks and counters ... can be implemented using combination of compare_exchange_?? operations and retry loops"

(a) and (c) are correct. The book names multiply, divide and shift; `%` is also missing, which is true. (b) omits **`fetch_xor`**. The integral types add all three bitwise ones: `fetch_and`, `fetch_or`, `fetch_xor`. I'm counting that as a precision gap, not a miss, and the new core item `5.2-atomic-integral-ops` re-tests the full set.

### Build

None: quiz-only subsection. F9 (`atomic_fetch_max` driver) is still open.

### Queue updates

```
python3 tools/review.py log 5.2-cas-weak-vs-strong hit --note "5.2.4 warm-up: strong = internal retry loop (no mutex); weak can fail spuriously; expensive f(expected) -> strong, cheap -> weak"
python3 tools/review.py log 2.3-scoped-thread-exclusive-ownership hit --note "5.2.4 warm-up: accessor lets outside code join; book's unconditional dtor join then fails; invariant = sole owner. Didn't mention silent detach() with his guarded dtor"
python3 tools/review.py log 4.1-notify-outside-lock miss --note "5.2.4 warm-up: said yes, notify before consumer waits is lost and consumer sleeps with item queued; believes pred is checked only after a wake"
python3 tools/review.py edit 4.1-notify-outside-lock --kind misconception --note "Same belief as original 4.1 Q7 resurfaced at med confidence (2026-09-27): wait(lk,pred) checks pred under the lock before sleeping"
python3 tools/review.py add 5.2 5.2-rmw-result-not-reload "After switching an operator form (x += n) to a named form (x.fetch_add(n, order)) the call returns the old value; how to get the value this call produced (old + n computed locally), and why a follow-up load() is wrong (another thread may have changed it in between)." --kind miss --source "atomic-operations-and-types-notes.md 5.2.4–5.2.5 Q2"
python3 tools/review.py add 5.2 5.2-atomic-ptr-arith-return "std::atomic<T*>: fetch_add/+= move by whole elements of T (sizeof(T) bytes each); fetch_add and x++ return the old pointer, += and ++x the new one. Consequence for claiming slots in a preallocated array (old-value form claims ring[0]; new-value form skips it and runs past the end one call early)." --kind core --source "atomic-operations-and-types-notes.md 5.2.4–5.2.5"
python3 tools/review.py add 5.2 5.2-atomic-integral-ops "Atomic integral ops: fetch_add/sub/and/or/xor plus the compound and ++/-- forms; which plain-int compound ops are missing (multiply, divide, shifts) and why that's rarely felt (counters and bitmasks); operator forms are always seq_cst, named forms take any ordering (RMW)." --kind core --source "atomic-operations-and-types-notes.md 5.2.4–5.2.5"
```

(The three `add` sources were first written as "5.2.4 Q2" / "5.2.4" / "5.2.5" and then `edit`ed to match this file's heading.)

### Fix list

- Opened: none.
- Closed: none. F1–F9 are still open.

### Beyond the book (ungraded)

Your answer is right. I compiled all three cases here (g++ 11.4, `-O2`):
- Result ignored, `m.fetch_or(4);`: `lock orl`.
- Result used, `return m.fetch_or(4);`: a `lock cmpxchgl` loop.
- Only the same bit tested, `return m.fetch_or(4) & 4;`: `lock btsl` (bit test-and-set). This is a single instruction that returns the old value of that one bit. It's the hardware version of `atomic_flag::test_and_set`, one bit wide.

### Correction to the 2026-09-26 check (5.2.3 Q1)

That check said your corrected loop "keeps `&& !expected`". The correction on disk (committed in e538e21, together with the check) is `while (!claimed.compare_exchange_weak(expected, true)) { expected = false; }`, with no `&& !expected`. That **is** the correct wait-until-free loop. The check quoted code that isn't in your answer. That's the "quote before grading" slip again, whether it came from misreading or from grading an earlier draft. Corrections aren't graded, so the queue is unaffected.

## 5.2.6–5.2.7 — The std::atomic<> primary class template; free functions for atomic operations

```
$ python3 tools/review.py due   (2026-09-27, second worksheet today)
Due now: 18 active (11 overdue) | maintenance due: 0 | due in next 3 days: 20 | total active: 60, retired: 1
WARM-UP PICKS (3):
  1. [miss] 4.2-abandoned-task-futures  (2d overdue)
  2. [miss] 5.1-data-race-definition  (2d overdue)
  3. [miss] 2.4-parallel-accumulate-requirements  (1d overdue)
Also due: 15 more (see due output)
Open fix-list items: 9 (F1-F9)
Lint: OK
```

Triage:
- Which user-defined types `std::atomic<>` accepts: **recall** (Q1).
- Why those restrictions exist (the book gives three reasons): **recall** (Q2).
- Compare/exchange on a type whose `==` disagrees with a byte-by-byte comparison: **code-tracing** (Q3, trading-flavoured).
- Free-function naming: `atomic_` prefix, `_explicit` suffix, pointers everywhere, both orders for the explicit compare/exchange, the `atomic_flag_` spelling: **recall** (Q5).
- Table 5.3 (which operations each type has): already covered by 5.2.1 Q5 and the 5.2.4–5.2.5 items. Not re-quizzed.
- The `std::shared_ptr` atomic free functions, shown in-text as a publish/read pattern: a **reusable mechanism**. **Build: `snapshot_cell`** (one sitting). Not also quizzed; the 5.2.6 link ("why not `std::atomic<std::shared_ptr<T>>`?") is in the self-explanation instead.

### Warm-up

1. Your `task_runner` still has three tasks sitting in its queue, never started, when its owner calls `shutdown()`. Some caller is holding the future for one of them. What does `fut.wait_for(std::chrono::seconds(0))` report right after `shutdown()` returns, while the `task_runner` object is still alive? What does it report once the `task_runner` has been destroyed, and what does `fut.get()` do then? Name the rule from 4.2 that produces the second answer.

Answer (med):  the question states that the tasks in the queue were never started when the shutdown was called and a future to one of the tasks was held in the caller. then there is additional information that shutdown() is called and returned, this means that the to_shutdown flag was turned on and the run_task function has also returned, this is because in the task runner implementation in `ch04_synchronizing_concurrent_operations/task_runner/include/task_runner.hpp` we can see that the check `if (to_shutdown) {return;}` is right after the `data_cond.wait()` block-and-sleep line - only after this do tasks get popped off and futures get fulfilled. so this means that the future held by caller will remain unset. until the `task_runner` object has been destroyed, the `fut.wait_for(std::chrono::seconds(0))` will return `std::future_status::timeout` since the corresponding packaged task is still alive in the queue, however, once the `task_runner` object is destroyed, then the waits dont block anymore because the future becomes `std::future_status::ready` but they have been set with an exception and not with the expected result which brings us to what happens when the packaged_task corresponding to an unset future is destroyed. What happens is that if we destroy the packaged task or its parent / container object, the future automatically becomes ready and is stored with an exception; it becomes ready with a std::future_error exception containing std::future_error::broken_promise; in this state .get doesnt block but throws the exception there immediately.

2. A colleague says: "two threads touching the same variable at the same time is a data race, so an atomic is just a race that happens to be fast." Give the Standard's definition of a data race with every one of its conditions, and point to the condition that means two threads calling `store()` on the same `std::atomic<int>` at the same time is not one.

Answer (med): 

**Data Race:** a data race is when two threads concurrently access a memory location and at least one of them is a write and there is no enforced ordering between them.

the last part "no ordering" is important because a read happening exactly at the time of a write might cause errors like 'torn reads' or more broadly 'data corruption'. So with atomic operations - this is the part that gets eliminated, i.e., the operations on atomic types are - as the name suggests - atomic or indivisible or individual. this means that only one thread does an operation on an atomic object at a time no matter how many threads share that object and this is like a mutex where a mutex serializes access to a whole block of code with multiple lines, atomic types are a more granular approach where we expect multiple threads to be racing their way through the lines of a single function all at once but we protect the shared data we must using atomic types and atomic operations, so even if we have multiple threads behind a line with a store() operation on an atomic<int>, they will all serialize or have forced ordering among them - to execute their store on that int - making the behavior defined rather than undefined.

3. `std::accumulate` works with plain input iterators and any `T` you can copy and add. The `parallel_accumulate` from Listing 2.9 asks for more on both counts. What exactly does it need from the iterator type, and from `T`, that `std::accumulate` doesn't? For each, which step of the algorithm creates the need?

Answer (med): parallel_accumulate requires that the type `T` be default constructable and the iterators be at least forward iterators. the iterator needs to be a forward iterator because parallel_accumulate divides the array into chunks and walks and accumulates each individually - section wise so this means that iterators need control, i.e., move accross distinct sections and they need to be copyable and overall function independently. the type needs to be default constructible also because for each section of std::vector<T>, the parallel_accumulate calls std::accumulate() separately and stores the result in a default constructed instance of type T which we give the functor by reference as we call the threads for the sections. so in essence, we need more flexibility with the type and iterator to compartmentalize the work across multiple threads.

### Quiz

1. (Recall) For each type, say whether the book's rules allow `std::atomic<X>`, and name the rule that decides it:

```cpp
struct TopPx  { std::int64_t bid_px; std::int64_t ask_px; };
struct Tick   { std::string venue;   std::int64_t px; };
struct Base   { virtual ~Base() = default; };
struct Mark   : Base { std::int64_t px; };
struct Level  { std::int32_t px; std::int32_t qty;
                Level& operator=(const Level& o) { px = o.px; qty = o.qty; return *this; } };
struct VenueIds { std::uint32_t ids[4]; };
```

   `Level`'s `operator=` does exactly what the compiler-generated one would. Does that change your answer for it? Why or why not?

Answer (med): 
```cpp
struct TopPx  { std::int64_t bid_px; std::int64_t ask_px; };
```
`TopPx` seems amenable to std::atomic<> compliance, since std::int64_t is just type definition for whatever integral integer type provides 64 bits. the rules are - trivial copy-assignment, i.e., default used for base classes and this class, this one is respected. the next is bitwise equality comparable, that too for integer types seems to be respected. final requirement is also complied to, i.e., no user supplied comparison operator as it shall be ignored and that too hasnt been provided.

```cpp
struct Tick   { std::string venue;   std::int64_t px; };
```
this one `Tick` seems recalcitrant to std::atomic<> and is not compliant to the first rule of having a trivial copy assignment operator. this is because its using std::string which is know to have heap based memory allocation and that means a copy operation would need new heap allocation and other corresponding lifecycle operations like capacity increase / decrease or freeing memory and so on, basically making the copy assignment non trivial as it doesnt lend it self to operations that make atomic types atomic like memcpy and memcmp. 


```cpp
struct Base   { virtual ~Base() = default; };
```
`Base` is also seemingly refractory to std::atomic<> prerequisite compliance because it has a virtual function and as we know, base classes of the type or the type in question itself cannot have virtual functions as that introduces V-Tables which hinders triviality of copy-assignment operator.

```cpp
struct Mark   : Base { std::int64_t px; };
```
`Mark` also recalcitrant to std::atomic<> compliance as it's inheriting from `Base` which has a virtual function.

```cpp
struct Level  { std::int32_t px; std::int32_t qty;
                Level& operator=(const Level& o) { px = o.px; qty = o.qty; return *this; } };
```
`Level` i am tempted to say seems to want to work with std::atomic<> as its typedefs std::int32 represent simple compliant integral types and its copy assignment operator features simple stack based operations, however, the rules are clear, default copy assigment operators only which use memcpy underneath so sadly this too isnt good to work with std::atomic<>.

```cpp
struct VenueIds { std::uint32_t ids[4]; };
```
`VenueIds` will work because it has only data member - and doesnt have comparison  or copy assignment operator and even though its an array, its the built in C-style array and not the heap based std::vector array, so in my estimation, it should be fit to work with std::atomic<>.

2. (Recall) The book gives three reasons why `std::atomic<UDT>` insists on a compiler-generated copy-assignment and byte-by-byte equality, instead of calling your own `operator=` and `operator==`. One of them goes back to a guideline from chapter 3. Give all three.

Answer (med): 
- First lets touch on the chapter 3 reference, i.e., dont pass pointers and references to protected data outside the scope of the lock by passing them as reference to user supplied functions. now consider example of a non-compliant type for std::atomic<>:
  ```cpp
  struct Level  { std::int32_t px; std::int32_t qty;
                  Level& operator=(const Level& o) { px = o.px; qty = o.qty; return *this; } };
  ```
  we know that for types with sizeof() larger than twice at most of void* are not going to have lock free atomic operations, i.e., the atomic template will have to use internal lock to make the operations atomic. that being said, take a look at the code, we see a pass by reference into the copy assigment operator and a return by reference which directly violate the law and setting the stage for deadlock down the line. 
- the other two reasons, i.e., not allowing copy assignment or comparison, i.e., operator=() and operator==() are similar so can be dealt with simultaneously. It is because under the hood the atomic<> uses memcpy() and memcmp() for copy and comparison respectively which are c-standard operations that know nothing about the type's semantics and nuances, they merely look at raw bytes for copying and comparing respectively and so make user defined implementations of these implementations redundant or unsuitable.

3. (Code-tracing) A price is stored as mantissa × 10^exponent, and `operator==` compares the *numbers*, so `{150, -2}` (1.50) equals `{15, -1}` (1.5):

```cpp
struct Price {
    std::int32_t mantissa;
    std::int32_t exponent;
    bool operator==(const Price& o) const;   // true when both represent the same number
};

std::atomic<Price> best{Price{150, -2}};     // 1.50, written by the feed for venue A

// Thread X: move the best price from 1.5 to 1.6, but only if it is still 1.5.
Price expected{15, -1};                      // 1.5, parsed from venue B's feed
bool ok = best.compare_exchange_strong(expected, Price{16, -1});
```

   No other thread touches `best`.
   (a) Is `ok` true or false? What do `best` and `expected` hold afterwards?
   (b) Thread X's author "fixes" it by looping: `while (!best.compare_exchange_strong(expected, Price{16, -1})) {}`. The loop now ends with `best == 1.6`. Why is this loop no longer an "only if it's still 1.5" check? (Think about what happens if another thread stores 1.7 between X's first and second attempts.)
   (c) What change to how `Price` values are *represented* makes the one-shot version behave as the author intended, without touching `std::atomic`?

Answer (med): ok so first of all just looking at the code we are violating the requirement that the type must be bitwise comparable which it is not since Price{150, -2} (1.50) equals Price{15, -1} which is a semantics thing not known to memcmp so the == operator which will actually get used will not even perform as expected or as the user supplied operator==() is made to do, even if it does compile which i think it will based on what i read and remember. 

(a) ok will be false since the byte comparison will fail for this type. expected will hold what best held before the comparison, `{Price{150, -2}}`. 

(b) with the addition of the while loop, every failed comparison will lead to an updated expected value to what best had before the failed comparison so best will eventually get updated to 1.6 as it is thanks to the update, i.e., a while loop `best == expected` with a constantly converging expected check, not a only if its 1.5 check. this is true even if we have another thread store 1.7 between the attempts, expected will just update to 1.7 before 1.6 is stored in it.

(c) removing the manual operator==() definition or using float to just directly store numbers instead of trying the scientific notation to store price might be the way to go, though using floats can lead to some inconsistencies in bit level representation so perhaps the first solution of relying on default operator==() and being more explicit / clear / stringent in using the two integer representation is a better approach, i.e., being clear about having only one digit on the left of the imaginary decimal in this representation might be a way to go.

5. (Recall) Rewrite each member call as the equivalent free-function call:

```cpp
std::atomic<int> a;  int e = 5;
std::atomic_flag f = ATOMIC_FLAG_INIT;

a.load();
a.store(7, std::memory_order_release);
a.compare_exchange_weak(e, 9, std::memory_order_acq_rel);
f.test_and_set(std::memory_order_acquire);
```

   For the third one: why does `e` turn into something different in the free-function version, and what must you pass that the member version let you leave out?

Answer (med): 

```cpp
std::atomic<int> a;  int e = 5;
std::atomic_flag f = ATOMIC_FLAG_INIT;

std::atomic_load_explicit(&a, std::memory_order_seq_cst);
std::atomic_store_explicit(&a, 7, std::memory_order_release);
std::compare_exchange_weak(&a, &e, 9, std::memory_order_acq_rel);
std::atomic_flag_test_and_set(&f, std::memory_order_acquire);
```
so the `e` could be passed as a value in the member function version but in this free function version `e` has to be passed by reference since thats how the free function interface has been defined. Along with that we also need to pass the actual object by reference which we want the free function operation done on - however this is a constant for all atomic free functions, not just the CAS.

Confidence-tag each answer (low/med/high).

### Build task: `snapshot_cell`

For a read-mostly value, publish each version as an **immutable snapshot** behind a `std::shared_ptr<const T>` instead of locking on every read:
- Readers grab the current snapshot once and use it for the whole quote.
- A writer builds a new object and swaps the pointer.

This is the book's `process_global_data()` / `update_global_data()` example, made reusable, plus an `update()` for writers that change one field of the current version.

The repo is C++17, so use the `std::shared_ptr` free functions from 5.2.7 (`std::atomic_load`, `std::atomic_store`, `std::atomic_compare_exchange_weak`/`_strong`). No `std::mutex` in your class. Whatever the library does internally to make those functions atomic is fine.

Interface (signatures only):

```cpp
template <typename T>
class snapshot_cell {
public:
    explicit snapshot_cell(std::shared_ptr<const T> initial);   // initial must not be null

    std::shared_ptr<const T> load() const;
    void store(std::shared_ptr<const T> next);

    template <typename F>
    void update(F f);   // f takes const T& and returns a new T
};
```

**Contract:**
- Any number of threads may call `load()`, `store()` and `update()` at the same time.
- `load()` returns a snapshot that never changes afterwards: nothing is ever modified after it has been published (hence `const T`). A snapshot stays valid for as long as the caller holds it, even if it's been replaced many times since.
- `update(f)` publishes `f(current)`. Concurrent `update()` calls must not lose each other's changes: the result must be as if they ran one at a time. `f` may be called more than once per `update()` (so it must have no side effects), and each call sees the snapshot that was current when that attempt started.
- `snapshot_cell` itself is neither copyable nor movable (like the atomic types).
- Default (`seq_cst`) ordering throughout; no memory-order parameters yet (5.3).

**Tests to write:**
1. **Readers always see a whole snapshot.**
   - Setup: `T` is a struct with two `long` fields `a` and `b`, and every value you publish has `a == b`. One writer calls `store()` with `(i, i)` for `i = 1 .. 200000`. Four reader threads call `load()` in a loop until the writer finishes.
   - Assert: on every load, `a == b`, and within one reader, `a` never goes down.
   - Catches: reading and writing the `shared_ptr` without the atomic free functions (TSAN reports it), or modifying a published object in place.
2. **No lost updates.**
   - Setup: `T` is a struct with one `long count`, starting at 0. Four threads each call `update()` 50,000 times with an `f` that returns a copy with `count + 1`.
   - Assert: after joining, `load()->count == 200000`.
   - Catches: an `update()` that loads, builds the new value, then `store()`s. Two threads read the same version and one increment vanishes, the same bug as the load-then-store `fetch_max` in 5.2.3.

**Self-explanation required before review** (2–4 sentences, closed-book): Why does `update()` need compare/exchange, when `load()` followed by `store()` would be simpler? Give an interleaving that loses an update. Then use the 5.2.6 rules to explain why you can't write `std::atomic<std::shared_ptr<const T>>` in C++17 and have to use the free functions instead.

Answer (med): the load and store are straightforward atomic operations with one statement each so handling fails and races doesn't need much elaborations or discussion. 
the interesting part is the update function which initially i just wrote the CAS operation directly into the retry while loop such that the expected updates and the retry's get the exchange done eventually however, that wouldnt work since we are using shared_prt for the internal current variable and the type T itelf is different so the supplied function needs to be called, the copy / next value obtained and then made a heap allocation and then that shared_ptr be used in the CAS so the pattern is similar but different to a more simple retry loop when multiple steps involved so we now know of two retry patterns with _strong version of CAS:

```cpp
   while (!compare_exchange_strong(expected, desired)){
      // do stuff if needed like resetting expected if updates to it not desired.
   }
```
and the one we use here:
```cpp
   while (true){
      // do stuff if needed like calculating desired value using steps like calling supplied functions as we did in exercise or resetting desired if we want and so on
   if(!compare_exchange_strong(expected, desired)){
      break;
   }
}
```
so thats what i have gleaned from this exercise for the most part.


Scaffolded: `snapshot_cell/` with `CMakeLists.txt`, empty `include/snapshot_cell.hpp` and `tests/snapshot_cell_driver.cpp`, and the `add_subdirectory` line in the root `CMakeLists.txt`.

### Beyond the book (optional, ungraded; not queued unless you say "queue it")

`struct Quote { double px; std::int32_t qty; };` is trivially copyable, so `std::atomic<Quote>` compiles. `sizeof(Quote)` is 16, not 12: there are 4 padding bytes after `qty` whose contents are never set to anything in particular. Given that compare/exchange compares **bytes**, what can go wrong with `compare_exchange_strong` on this type even when every field of `expected` matches? What did C++20 change about this? (Also worth checking: C++20 finally adds `std::atomic<std::shared_ptr<T>>` and deprecates the free functions you're about to use.)

Answer: 

The issue is the **4 padding bytes**.

Suppose:

```cpp
struct Quote {
    double px;       // 8 bytes
    std::int32_t qty; // 4 bytes
    // 4 padding bytes
};
```

You might have:

```text
expected:
px   = 100.5
qty  = 10
pad  = 0xAA AA AA AA

actual:
px   = 100.5
qty  = 10
pad  = 0xBB BB BB BB
```

The **fields are identical**, but the complete 16-byte object representation is different.

### Before C++20

For atomic compare-exchange, the comparison could effectively be based on the **object representation (bytes)**.

So:

```cpp
compare_exchange_strong(expected, desired)
```

could **fail even though `expected.px == current.px` and `expected.qty == current.qty`**.

That's the nasty part: padding bytes aren't meaningful to you, but they can affect the comparison.

### C++20 change

C++20 changed `compare_exchange` to compare the **value representation**, rather than the entire object representation.

So padding bytes that don't participate in the value are no longer supposed to cause this kind of false failure.

In simplified terms:

```text
Before C++20:
compare bytes → padding can matter

C++20:
compare value representation → padding doesn't matter
```

This is one reason the question specifically emphasizes **"bytes"** and the otherwise seemingly harmless padding.

And yes, the other note is important: C++20 provides `std::atomic<std::shared_ptr<T>>`, whereas the C++17 `std::atomic_load(&ptr)` / `std::atomic_compare_exchange_*(&ptr, ...)` free functions are the older approach.


## Review: 5.2.6–5.2.7

### Warm-up

- **`4.2-abandoned-task-futures` -> hit.**
  > "until the `task_runner` object is destroyed, the `fut.wait_for(...)` will return `std::future_status::timeout` since the corresponding packaged task is still alive in the queue"
  > "if we destroy the packaged task or its parent / container object, the future automatically becomes ready ... with a std::future_error ... broken_promise; in this state .get doesnt block but throws"

  Correct on both states, and the rule is named. Nits:
  - The error code is `std::future_errc::broken_promise`; `future_error` is the exception type.
  - "run_task function has also returned": `shutdown()` only sets the flag and notifies; the destructor is what joins. The worker may still be finishing when `shutdown()` returns. It doesn't change the answer, because the queued task can never start either way.

- **`5.1-data-race-definition` -> miss.** The item was upgraded to `misconception`.
  > "a data race is when two threads concurrently access a memory location and at least one of them is a write and there is no enforced ordering between them."
  > "with atomic operations - this is the part that gets eliminated ... they will all serialize or have forced ordering among them"

  The condition **"at least one of the accesses is not atomic"** is missing again, and it's exactly the one the question asked you to point to. Two concurrent `store()`s on one atomic are **not** ordered with respect to each other: 5.1 says which one touches the memory first is still unspecified. They're legal because *both accesses are atomic*, not because atomics impose an order. The 5.1 wording lists all of them: same memory location, from separate threads, at least one is a write, at least one is not atomic, and no enforced ordering between them. All must hold for a data race.

- **`2.4-parallel-accumulate-requirements` -> hit.**
  > "the type `T` be default constructable and the iterators be at least forward iterators"
  > "stores the result in a default constructed instance of type T"

  Both requirements and the `results` vector as the cause of the `T` one are right. On the iterator side, the concrete steps are `std::distance` and `std::advance` walking the range more than once to find each block's boundaries. An input iterator is single-pass, so it can't do that. "move across distinct sections and ... be copyable" is the right idea.

### Quiz

**Q1 -> hit** (confidence: med)
> "`TopPx` seems amenable to std::atomic<>" · "`Tick` ... not compliant to the first rule of having a trivial copy assignment operator" · "`Mark` also recalcitrant ... inheriting from `Base` which has a virtual function" · "`Level` ... the rules are clear, default copy assigment operators only" · "`VenueIds` will work"

All five are right, and the `Level` follow-up ("no, it must be compiler-generated") is exactly the point. I checked in scratch: libstdc++ rejects `Tick`, `Mark` and `Level` with "std::atomic requires a trivially copyable type".

One wrong belief, which shows up again in Q3:

> "final requirement is also complied to, i.e., no user supplied comparison operator"

There's no such requirement. A type with its own `operator==` is allowed in `std::atomic<>`. The operator simply isn't used: compare/exchange compares bytes. The book raises this case explicitly ("similar behavior ... with a user-defined type that has an equality-comparison operator defined"), and that's why it's a surprise rather than a compile error.

**Q2 -> miss** (confidence: med). One of three reasons.
> "dont pass pointers and references to protected data outside the scope of the lock by passing them as reference to user supplied functions"

Reason 1: correct, and tied to ch3.

> "under the hood the atomic<> uses memcpy() and memcmp() ... make user defined implementations of these implementations redundant or unsuitable"

This restates *what* the rule is, not *why*. The other two reasons:
- **One shared lock.** The library may use a single lock for every atomic operation that needs one. Your `operator=` or `operator==` would run while holding it, so a slow comparison blocks unrelated atomics everywhere, and code that touches another atomic could deadlock. You mentioned "deadlock down the line" under reason 1, but not the shared lock that causes it.
- **Lock-free chance.** If the type is just raw bytes, the compiler can use atomic instructions directly for small types, so the instantiation can be lock-free. User code in the copy or compare would rule that out.

Queued as `5.2-atomic-udt-reasons`.

**Q3 -> hit** (confidence: med)
> "(a) ok will be false since the byte comparison will fail for this type. expected will hold what best held before the comparison, `{Price{150, -2}}`."
> "(b) ... not a only if its 1.5 check. this is true even if we have another thread store 1.7 between the attempts, expected will just update to 1.7 before 1.6 is stored"
> "(c) ... being more explicit / clear / stringent in using the two integer representation ... having only one digit on the left of the imaginary decimal"

(a) and (b) are right: the loop turns "replace 1.5" into "replace whatever is there". I confirmed (a) in scratch: `ok = 0`, `expected = {150,-2}`, `best` unchanged, and the second attempt succeeds. For (c), your final choice, a single normalized form, is the fix: **each value must have exactly one bit pattern.** The usual trading version is an integer number of ticks (`std::int64_t price_ticks`).

Your other two options don't work:
- Removing `operator==` changes nothing, because compare/exchange never called it.
- `float` has the same problem; the book names floating point as the example.

Same wrong belief as Q1:

> "we are violating the requirement that the type must be bitwise comparable which it is not"

`Price` *is* bitwise comparable: it's two `int32_t`s with no padding, and it compiles. The problem is that its `==` disagrees with the byte comparison.

**Q5 -> miss** (confidence: med)
> "`std::atomic_load_explicit(&a, std::memory_order_seq_cst);`" and "`std::atomic_store_explicit(&a, 7, std::memory_order_release);`"

Both correct. The plain equivalent of `a.load()` is `std::atomic_load(&a)`, but yours means the same thing.

> "`std::compare_exchange_weak(&a, &e, 9, std::memory_order_acq_rel);`"

Three errors: the `atomic_` prefix is missing, the `_explicit` suffix is missing, and the explicit form **requires both orders**. It should be `std::atomic_compare_exchange_weak_explicit(&a, &e, 9, std::memory_order_acq_rel, std::memory_order_acquire)`. The failure order follows 5.2.3's derivation rule, acq_rel → acquire.

> "`std::atomic_flag_test_and_set(&f, std::memory_order_acquire);`"

Any memory-order argument needs the `_explicit` form: `std::atomic_flag_test_and_set_explicit(&f, std::memory_order_acquire)`.

> "`e` could be passed as a value in the member function version but in this free function version `e` has to be passed by reference"

This is backwards. The member function takes `e` **by reference**. The free function takes a **pointer** (`&e`), because the free functions are designed to be C-compatible, and C has no references. The atomic object is passed by pointer too, not by reference. The "what must you pass that the member version let you leave out" half (the failure order) isn't answered. Queued as `5.2-atomic-free-functions`.

### Build

```
snapshot_cell: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK
```

The sanitizers are clean because there's no data race. **The library still has a real bug: `update()` loses updates.**

**Library: bug in `update()`.**
- `load()`, `store()`, the constructor, and deleting copy and move are all fine.
- The retry loop's exit is inverted: `if (!std::atomic_compare_exchange_strong(&current, &expected, desired)) break;` leaves the loop when the CAS **fails** and loops again when it **succeeds**. Trace it:
  - **No contention:** the CAS succeeds, so the loop runs again. `f` is called a second time on the old snapshot and a second object is allocated. That CAS fails, because `current` has moved on, so the loop exits. The net effect is correct, but `f` runs twice per call. Scratch, one thread: 1000 updates → `count` = 1000, `f` called 2000 times.
  - **Contention:** another thread published first, so the first CAS fails. The loop exits **without publishing anything**, and that update is silently lost.
- Scratch stress (your header unmodified, copied to scratch, 4 threads × 50,000 `update()`s): **10/10 runs lost updates**, final counts 166,642–180,792 out of 200,000 (roughly 10–17% lost).
- The fix is the exit condition. Leave the loop when the CAS succeeds, and when it fails, go round again with the refreshed `expected`. The structure is the same as your `fetch_max` loop, which was correct.

Note on the header: `struct config` is test data inside the reusable header (the same issue as F6/F9). Move it to the driver. `<thread>` is unused.

**Driver, checked against the Tests-to-write list:**
- **Test 2's assert can't fail**, for two reasons:
  - It checks **`cell`**, test 1's cell, whose last store was `{200000, 200000}`, instead of `cell2`.
  - It runs **before** the updater threads are joined.

  I made just those two changes in a scratch copy: your own driver then **failed 20/20** (for example `fee = 171852`). So the test design is right, and only the assert was pointed at the wrong thing. That's why the library bug got through.
- **Test 1** is written, and the `a == b` assert is there. The second listed assert, "within one reader, `a` never goes down", is missing.
- Not a fix item: `persistent_config_refs` isn't needed. The comment says the loaded object could die when the writer stores again. It can't: `load()` returns a `shared_ptr` that owns its snapshot, so that version stays alive until the reader drops it. That's the whole point of the design, and it's in the contract. Scratch, with the vector removed: ASan+UBSan 10/10 clean, TSAN 5/5 clean.

**Self-explanation: incomplete.**
> "while (true){ ... if(!compare_exchange_strong(expected, desired)){ break; } }"

This writes down the inverted exit as the pattern you learned. The two things the prompt asked for are missing:
1. An interleaving where load-then-store loses an update. For example, A and B both load version 5 (count 10), A stores 11, then B stores 11. One increment is gone.
2. Why C++17 can't use `std::atomic<std::shared_ptr<const T>>`. By 5.2.6's rules, `shared_ptr`'s copy-assignment isn't trivial: it runs code to adjust the reference count. So it can't go in `std::atomic<>`, which is why the book says the free functions "break from the principle".

**Status:** F10 opened (library fix, driver asserts, self-explanation). The build isn't done until F10 is re-verified.

### Queue updates

```
python3 tools/review.py log 4.2-abandoned-task-futures hit --note "5.2.6 warm-up: timeout while runner alive; after destruction ready with future_error broken_promise, get() throws; rule = destroying an unrun packaged_task/unset promise stores broken_promise"
python3 tools/review.py log 5.1-data-race-definition miss --note "5.2.6 warm-up: gave location/one write/no ordering, again omitted 'at least one not atomic'; said atomics make it legal by enforcing ordering"
python3 tools/review.py edit 5.1-data-race-definition --kind misconception --note "2026-09-28: repeat miss with a wrong model (atomics legal because they serialize/order), not just an omission"
python3 tools/review.py log 2.4-parallel-accumulate-requirements hit --note "5.2.6 warm-up: forward iterators (chunks walked independently, multi-pass) and default-constructible T (results vector)"
python3 tools/review.py add 5.2 5.2-atomic-udt-reasons "The book's three reasons std::atomic<UDT> uses memcpy/memcmp instead of user operator=/operator==: (1) ch3 guideline, no passing references to protected data to user code under the internal lock; (2) the library may use one lock for all non-lock-free atomics, so user code under it risks deadlock or blocks unrelated threads; (3) raw bytes make lock-free atomic instructions possible." --kind miss --source "atomic-operations-and-types-notes.md 5.2.6–5.2.7 Q2"
python3 tools/review.py add 5.2 5.2-atomic-free-functions "Free-function forms: atomic_ prefix, _explicit suffix takes the order(s), object and expected passed as pointers (C-compatible), atomic_compare_exchange_*_explicit needs BOTH success and failure orders, atomic_flag_test_and_set(_explicit)/atomic_flag_clear(_explicit)." --kind miss --source "atomic-operations-and-types-notes.md 5.2.6–5.2.7 Q5"
python3 tools/review.py add 5.2 5.2-atomic-udt-requirements "Which UDTs std::atomic<> accepts (trivial copy-assignment all the way down, no virtuals, bitwise comparable); a user operator== is allowed but ignored, so compare/exchange can fail for equal values with different representations (floats, Price{150,-2} vs {15,-1}); fix with one canonical representation per value." --kind core --source "atomic-operations-and-types-notes.md 5.2.6–5.2.7"
python3 tools/review.py add 5.2 5.2-atomic-udt-size-lockfree "std::atomic<UDT> size guidance: <= int/void* usually lock-free, 2x with DWCAS on some platforms, bigger uses an internal lock the library may share across all such atomics; interface limited to load/store/exchange/compare-exchange, so a one-field change means replacing the whole value; complex structures belong behind a mutex." --kind core --source "atomic-operations-and-types-notes.md 5.2.6–5.2.7"
python3 tools/review.py add 5.2 5.2-shared-ptr-snapshot "Publishing immutable snapshots via the shared_ptr atomic free functions: why they exist (shared_ptr isn't trivially copyable, so no std::atomic<shared_ptr> by 5.2.6's rules in C++17); readers load once into a local shared_ptr, which keeps that version alive; a writer's read-modify-publish needs a compare/exchange loop that exits on SUCCESS." --kind core --source "atomic-operations-and-types-notes.md 5.2.6–5.2.7 build snapshot_cell"
```

### Fix list

- **Opened F10:** `snapshot_cell`:
  - (a) the `update()` loop exits on failure;
  - (b) Test 2's assert checks `cell` instead of `cell2`, before the join;
  - (c) Test 1 is missing the "never goes down" assert;
  - (d) the self-explanation.
- Closed: none.

### Beyond the book (ungraded)

- **Padding (your answer):** right. C++20 (P0528) makes compare/exchange ignore padding bits. Recent GCC implements it by clearing the padding (`__builtin_clear_padding`) before comparing. In C++17 a struct with padding can fail a CAS forever even when every field matches. So for atomics, prefer structs with no padding, or add an explicit, zeroed filler field.
- **16-byte atomics and `cx16`:** the CPU supports `cmpxchg16b`, but GCC's `std::atomic<16-byte T>` goes through libatomic and reports `is_lock_free() == false`. Don't rely on a DWCAS-based design being lock-free without checking `is_lock_free()` on the actual toolchain.
- **C++20:** `std::atomic<std::shared_ptr<T>>` exists, and the free functions you used are deprecated. On libstdc++ both are typically *not* lock-free (they use a lock internally). The read side is still cheap compared with a mutex around the whole table, but it isn't free.

### F10 re-check (2026-09-28)

```
snapshot_cell: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK
```

- **(a) Library: fixed.** `update()` now leaves the loop when `atomic_compare_exchange_strong` succeeds. On failure it retries with the refreshed `expected`. Scratch stress, with your header copied unmodified:
  - 1 thread: 1000 updates → 1000, with `f` called exactly 1000 times (it was 2000).
  - 4 threads × 50k: 10/10 runs gave exactly 200,000.
  - 8 threads × 50k: 3/3 runs gave exactly 400,000.
- **(b) Driver Test 2: fixed.** The assert now checks `cell2`, after the updater threads are joined. It can fail now. Mutation check: I ran your new driver in scratch against two deliberately broken copies of the header. It caught the old inverted-exit `update()` **20/20** and a load-then-store `update()` **20/20**, and passed 20/20 against your fixed header.
- **(c) Test 1's "`a` never goes down within one reader" assert: still missing.**
- **(d) Self-explanation: still owed.**

F10 stays open, narrowed to (c) and (d). The `struct config` move is still optional.
