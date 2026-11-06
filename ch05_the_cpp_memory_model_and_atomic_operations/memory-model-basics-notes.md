# 5.1 — Memory model basics

Triage: pure formal vocabulary/definitions (object vs. memory location, the precise data-race definition, modification order) — no data structure or reusable pattern derived in-text, so **quiz-only, no build task**. Not treated as scene-setting like 3.1/4.4: this is load-bearing vocabulary that 5.2 (atomics) and 5.3 (ordering) build directly on top of, and it sharpens a distinction (mutex-avoids-the-race vs. atomic-avoids-the-UB-but-not-the-race) that connects straight back to ch3's mutex work. Dense enough to warrant a real quiz, not a 4.4-style two-question appendix.

## Warm-up (tier (c) — cross-chapter design-judgment, no due-in gate; nothing is numerically due at session 16)

Both `scoped_thread` (2.3) and `pending_requests` (4.2.3) enforce a kind of exclusive ownership over a resource that must eventually be "completed" exactly once — `join()`, or `set_value()`/`set_exception()`. Design-judgment: for each of the two, what would go wrong if that completion step were allowed to happen zero times, or more than once? And how does each design actually prevent that in practice — `scoped_thread`'s RAII destructor vs. `pending_requests`'/`promise`'s move-only, single-owner contract?

Answer: for pending_requests file where we make use of the use of future + promise pair to show how to handle events external to the system like data coming in from the server. In just the case of pending_requests, the fulfill() function is responsible for fulfilling the promise by settings its value and then deleting the promise from the map that stores it along with the promise's registration id, that being said if it were to not complete the happy path described above and the promise was never set (and we have tested this in the driver for this file), i.e., the promise is destroyed before setting its future, this leads to the future not blocking on get but in it is stored an exception of code broken_promise. there ought to also be a set_exception() clause in the fulfill() function that caters to other possible exception conditions, however as the code stands the above is all that happens, i.e., the promise can either be set once or not at all because there is a mutex over the function which doesnt unlock before the promise is deleted from the map post set_value(), where as we can have promises in the map as we destroy the class instance. the destructor itself hasnt been implemented and we are relying on the default compiler generated destructor for the pending_requests implementation. though there is the aspect that promises cannot be copied and only moved like unique_ptr and like future and so when the default destructor does run to delete the map storing the promises, we don't risk an external copy trying to set_value() nor do we need to have fear of a reference because the purpose of having moved the object into another scope is to ensure no references can be taken to the object outside the new scope which avoids another possible failure point.

For the other scoped_thread implementation - we have a joinable() check in destructor as well as in constructor, though the one in destructor is superflous since once the thread is `moved` into the scoped_thread object, its former thread object and references cannot call join() on the underlying thread. So having moved the thread into the internal private thread object allows only the scoped_thread object's destructor to join the thread which is how it prevents any possibility of joining more than once or not at all.

## Quiz

1. (Recall) The C++ Standard defines an *object* as "a region of storage." A single object can occupy more than one *memory location*, and two objects can also share a single memory location. State the general rule for when a struct's members count as separate memory locations, and the one exception to it.

Answer (med): all native types like int, bool, float, double, and so on occupy at least one memory location as each byte has its own memory location. However, objects like bit fields can share a single memory location, i.e., we can pack things like flags which require one bit, or how we store information in network headers where every bit is important, we can use things like bit fields to pack multiple objects into one memory location and so we cant do some of the normal things we can do with other normal variables like we cant dereference bit fields since they are not the sole owners of their memory address. the general rule is that other that bit fields, member with native types have their own separate memory locations while constructs made on top of them, like an array of ints, have their memory location and their members have their memory locations.

2. (Code-tracing) Given:

```cpp
struct widget_data {
    int id;
    unsigned flag_a : 1;
    unsigned flag_b : 1;
    unsigned flag_c : 4;
    double   price;
};
```

How many distinct memory locations does one `widget_data` object occupy? Name them.
Answer (med): there are four distinct memory locations for this object, there is one associated with the widget_data object itself, then there is one for the three bit fields flag_a, b, c and then there are two for id and price, making a total of four.

3. (Recall) State the C++ Standard's precise formal definition of a *data race* — all three conditions have to hold together. (Not "two threads touch the same variable badly" — the actual three-part definition.)

Answer (med): A race condition is specifically called a data race when two or more threads operate on a single memory location concurrently, at least one of them is a write and there is no enforced ordering among their operations.

4. (Application) Two ways to avoid a data race on a given memory location are a mutex and an atomic type. Precisely: does using `std::atomic<T>` instead of a plain `T` actually *prevent the race* — i.e. does it guarantee which thread's operation happens first — or does it do something narrower than that? Contrast with what a mutex (ch3) actually guarantees, and connect this to why "atomic" doesn't mean "deterministic."

Answer (med): using atomic types does not prevent all race conditions however it does prevent data races, i.e., it wont fix the race conditions caused by poor interface design like say having push(), pop() and top() in the stack interface for a thread_safe stack, that being said, using atomics means that reads and writes on an operational level - to an atomic type - will be atomic and no thread will be able to see a half done atomic operation on an atomic type, they will see it as done or not done and this prevents UB (undefined behavior) caused by data races and prevents things like torn reads. using atomics still doesnt decide what happens first, read or write, it just ensures that one happens after the other on the atomic type, i.e., the operations themselves are atomic. there is such a thing as modification order which is respected for atomic types, i.e., threads agree on the order of mutation or the write history, i.e., if the write history of x is [1,2,3] then threads may see [1,3] or [2,3] or [1,2], i.e., they can be late or skip but cant read stale values with respect to their own last read, this is a given even in memory_order_relaxed, i.e., the modification order is respected. This is however not the same as memory_order which is more about the order of the atomic operation relative to the other operations of this thread and those of others, memory_order arguments supplied to atomic operations are responsible for determining the relative ordering of operations and their visibility across the threads sharing that function.

5. (Recall) What is an object's *modification order*? For a non-atomic object, whose job is it to make sure all threads agree on that order, and what happens if they don't? For an atomic object, whose job is it?

Answer (med): it is the job of the compiler to ensure that threads agree on the modification order if we use std::atomic on a variable. modification order is the history of writes done on a variable through its lifespan. now why this exists is because of caches L1, L2, L3 and RAM, i.e., changes made to a copy of x on one thread's cache is going to take time to propagate to the L1, L2 of the other threads, cache coherence is understood to take place as cache lines are sent across the core's and the different cache levels depending upon the cycles set for them. But on a higher level, its important to understand that all threads - for atomic variables - agree on the write history of the atomic varable, i.e., if the write history for an atomic int x is [1,2,3,4] then threads will see [1,2] or [1,3] or [2,3] or [2,3,4]. the rule is - skipping or missing values is allowed, being late to the newest value is allowed, however, reading backwards is not allow, i.e, other threads cant see back in time like [4,3,1] for example.

6. (Application) The book says a thread's own subsequent reads/writes must stay consistent with what it has already observed in an object's modification order — i.e. a thread can't act as if it saw a write it hasn't actually synchronized with yet. Give a concrete one-sentence example of what this rules out (a compiler optimization or a runtime behavior that would otherwise be tempting/legal-looking).

Answer (med): in single threaded code, compilers try to optimize code by doing things like speculative execution where the CPU guesses what might be needed ahead of time or makes reads before the actual instruction in the program is reached. this speculative execution is bad in multithreaded code even if it looks legal because it might lead to one thread making a read in the code before another thread got a chance to make a write on a that variable. memory model prohibits this for atomic variables, i.e., if a read for a value V is at position N then its read is not allowed to be done before the position V instruction is reached.

7. (Recall) Do two different threads need to agree on the *relative* order of modifications to two *different* objects (object A's modification order vs. object B's, compared against each other)? What does the answer set up for 5.3?

Answer (med): yes, threads must agree on the relative order of operations on two different objects and not just for one object where which thread operates on that object first. this is best illustrated with the example:

```cpp
// Thread A
data = 42;      // (1) write to memory location 'data'
ready = true;   // (2) write to memory location 'ready'
```
```cpp
// Thread B
if (ready)               // (3) read of 'ready'
    std::cout << data;   // (4) read of 'data'
```

now you see the relative operation order on thread A code and think that of course the cout in thread B will run, however, merely agreeing on the modification order and relying on cache coherence doesnt give you the guarantee that thread B will respect the relative write operation order of code in thread A and may very well skip the cout in the case where even though data was written and visible, the write to ready as true was not visible.

Two real mechanisms can break the natural assumption of mutually respected relative ordering above:

- **Compiler reordering** — to an ordinary (non-atomic) compiler, `data = 42;` and `ready = true;` are two unrelated statements touching unrelated memory. Nothing stops it from emitting them in either order, or interleaving them with surrounding code, since doing so doesn't change Thread A's own single-threaded observable behavior.
- **Hardware/CPU reordering** — even if the compiler keeps program order, a CPU's store buffer and out-of-order execution can let the write to `ready` become globally visible _before_ the write to `data`, precisely because they're unrelated memory locations with no dependency tying them together.

_That_ is "ordering, not staleness": the guarantee about what order another thread is allowed to observe your writes _across different memory locations_ — and about how far the compiler/CPU may reorder things relative to each other. This is exactly what the **C++ memory model** and `std::memory_order` govern. `memory_order_release`/`memory_order_acquire` (§3.3) is the specific tool that rules out the bad interleaving above, by forcing the write to `data` to happen-before `ready` becomes visible to whichever thread acquires it. This distinction — **coherence keeps one location honest over time; the memory model governs relative order across locations, and constrains how much the compiler/CPU may reorder** 

Confidence tag each answer (low/med/high) before self-checking, per the calibration rule.

## Review — 5.1

Warm-up: fully correct, ties both build tasks precisely — a nice catch specifically noting the destructor's `joinable()` check in `scoped_thread` is technically superfluous once ownership has been exclusively transferred via move, and correctly ties `pending_requests`'/`promise`'s move-only contract (same as `unique_ptr`, no copy) to why no external code can double-set or race a `set_value()`. No corrections.

Q1 — the core idea (adjacent bit-fields share a memory location, everything else gets its own) came through, but the phrasing has real slack, and one word matters: it's specifically **adjacent** bit-fields that share a memory location — a bit-field separated from another by a non-bitfield member (or an allocation-unit boundary) does *not* share a location with it. The tangent about arrays and about not being able to take a bit-field's address (true, but not what was asked) muddies more than it sharpens. Tightened version: every ordinary (non-bit-field) member of a struct is its own distinct memory location; adjacent bit-fields are packed into one shared memory location; a bit-field separated from its neighbors isn't "adjacent" and gets its own.

Q2 — a real miss, not just phrasing: the correct count is **3** memory locations, not 4 — `id` (1), the three adjacent bit-fields `flag_a`/`flag_b`/`flag_c` packed together (1), `price` (1). The object as a whole isn't a separate fourth location stacked on top of its members — the object's storage *is* the union of those three. This falls straight out of the Q1 rule once that's tightened.

Q3 — close, but missing the condition the question specifically flagged: it takes *three* things holding together, and only two showed up. You have "no enforced ordering" and "at least one is a write" — the missing third clause is "at least one of the two accesses is **not atomic**." That clause is what actually separates a data race from two threads legitimately hammering the same atomic variable at once, which is fine by definition precisely because atomic accesses are exempted from it. Full shape: no enforced ordering between two accesses to a single memory location from separate threads, at least one access not atomic, at least one access a write → data race, UB. Worth having this exact three-part form ready — it's the kind of thing that gets probed directly.

Q4 — strong. Correctly separates "atomics prevent the UB/torn-read consequence of a data race" from "atomics don't fix bad interface-level race conditions" (push/pop/top), and correctly distinguishes modification order (always respected, even under `relaxed`) from `memory_order` (cross-location visibility/ordering) — exactly the seam 5.1→5.3 is setting up. One thing left on the table: the question also asked for a contrast with what a *mutex* guarantees, and the answer stayed almost entirely on the atomic side. Worth having ready: a mutex doesn't guarantee ordering either — only mutual exclusion (one thread in the critical section at a time); which thread wins a contested lock is just as unspecified as which atomic op "goes first." That symmetry is the cleanest way to state "atomic doesn't mean deterministic."

Q5 — the atomic half (compiler's job, cache-coherence framing as the *why*) is correct and a genuinely good addition beyond the book's text. But the question had two explicit halves and only one got answered: for a *non-atomic* object, whose job is it to keep threads agreeing on modification order, and what happens if they don't? That's the programmer's job (sufficient synchronization — mutexes or any other enforced ordering), and failing to do it produces exactly the data race from Q3 (UB), not just "confusing behavior."

Q6 — right neighborhood, not quite the rule being asked about. This isn't about a read racing an earlier write across threads in general — it's narrower: a single thread's *own* reads of the same object must stay consistent with what that *same thread* has already itself observed. Concrete example: if thread T has already read `x == 3` (a later point in x's modification order), a subsequent read by that same thread can't legally come back with `x == 2` — even if a stale cached copy of 2 is still floating around somewhere — because that would mean the thread is going backward relative to what it's already witnessed. That's what actually gets ruled out.

Q7 — the one real slip worth naming directly: it opens with "yes, threads must agree on the relative order of two different objects' modifications" — wrong direction. The correct answer is **no** — by default, C++ does not guarantee different threads agree on the relative order between two different objects' modification orders; each object's modification order is its own independent thing. But your own worked example (`data`/`ready`) and the reordering discussion underneath it demonstrate exactly this, correctly and in real depth (compiler reordering, store-buffer/CPU reordering, release/acquire landing precisely on why 5.3 exists). The substance is already right — it just needs the one-line headline flipped from "yes" to "no" to match the reasoning underneath it.

**5.1 → tested, due in 2 (session 18), hits 0.** Q2, Q3, Q5 are the substantive misses; Q1, Q6, and Q7's headline are precision/framing gaps with the right instinct underneath (Q7's own follow-through is in fact the correct answer, just contradicting its own opening line). No redo required — corrections given directly above. Worth flagging only as a pattern to watch, not alarm: a denser miss-rate than 4.1/4.3 (both near-clean quiz-only passes) — reasonable for genuinely new, abstract vocabulary, but if precision gaps of this shape (missing a named condition, off-by-one counting) recur in 5.2/5.3, that's worth a misconceptions-log entry rather than treating each as a one-off.
