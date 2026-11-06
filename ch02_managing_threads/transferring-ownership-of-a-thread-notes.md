# 2.3 — Transferring ownership of a thread

Session 4.

Warm-ups (2 — priority order: misconceptions log first, then whatever's due):

0a. (Misconceptions log, needs 1 more clean hit after 2.2's warm-up.) A `std::thread` is still running its task. Someone calls `.detach()` on it, and later, elsewhere, calls `.detach()` again on the same object by mistake. What actually happens? Now compare: what would've happened instead if nobody had called `detach()` or `join()` at all before this `std::thread` object's destructor ran?

Answer: calling detach on a running thread makes it loose association with the std::thread object and the underlying thread is handed over to the runtime for its resource management until it finishes - it also then returns false to joinable(). calling on a thread which now has no association to the thread underneath leads to a std::system_error. To the second scenario, if join or detach had not been called and a std::thread destructor ran, then that means it went out of scope and std::terminate was called. this means that no matter where that thread was in its execution of its task, its resources were taken away and the task was abandoned ungracefully.

0b. (2.1 is due this session.) When you pass a callable object to `std::thread`, why does the book emphasize that "the copy must behave equivalently to the original"? Give a concrete way that could fail in practice.

(4.1's still-open items and 2.1's self-explanation are tracked in the ledger's Outstanding Items table, not repeated here.)

## Quiz — closed-book, tag each answer's confidence (low/med/high) before checking against the book

Recall:
1. Why is `std::thread` movable but not copyable? What resource does a `std::thread` instance actually own, and why wouldn't copying it make sense?

Answer (high): a std::thread owns an processor thread underneath, basically a thread at system level represents time and resources on a CPU core so its not as if its a physical entity in its entirety but it represents time on a core which is a physical entity that allows for resources to do a task. this leads into why std::thread is movable but not copyable because std::thread as an object holds ownership to a system thread and copying it doesnt make sense and a more mechanical / practical reason for it not having a copy or assign constructor is because those constructors are deleted so if u pass a thread object around, its being moved into or out of that function or thread object. the reason for deleting these constructors is the same as why we have a move only unique_ptr often representing large object or file handlers, there should only be one object responsible for managing a system thread - copies would allow multiple objects the ability to call join or detach the same underlying thread, which breaks that invariant.

**Correction - 2026-10-05**
A std::thread represents ownership of an underlying system thread of execution, which is scheduled onto CPU cores. It is movable but not copyable because exactly one std::thread object should own and manage a given system thread at a time. Allowing copies would give multiple objects the ability to join() or detach() the same underlying thread, breaking that ownership invariant. This is the same design principle as std::unique_ptr: ownership is exclusive, so copying is prohibited and moving transfers ownership.

2. When does transferring ownership of a `std::thread` require an explicit `std::move()` call, and when does it happen automatically?

Answer (med) : if the thread was constructed and not immediately associated to an lvalue, so say i call a function and give it a thread this way:
```cpp
void func1(std::thread t);
void func2(int i);
func1(std::thread(func2, 2));
```
in the above case the thread is being constructed as an rvalue and the std::move is not required as the move is implicit.

However in the case where the thread was already associated to an lvalue before being passed to a function or another thread, there the move has to be made explicit and so we do some thing like this:

```cpp
void func1(std::thread t);
void func2(int i);
std::thread t1 = std::thread(func2, 2);
func1(std::move(t1));
// or if we are to move it to another thread:
std::thread t2 = std::move(t1);
```

Code-tracing:

3. In the book's `t1`/`t2`/`t3` example, the *final* move — moving the thread running `some_function` back into `t1` — calls `std::terminate()`. Trace exactly why, tied to what `t1` already holds at that point. Then connect it explicitly to the rule from 2.1 about destructors and joinable threads — is this a new rule, or the same one showing up in a different spot?

Answer (med): with destructors the rule is, unless you decide what to do with the thread before a thread's destructor is run, i.e., detach or join it, and it goes out of scope of the caller, then std::terminate() is called and its resources are snatched. 
if we try to then move a thread to another thread object that already holds an underlying thread, then thats like dropping an underlying thread without wrapping it up or deciding what to do with it, in favor of the new incoming thread and so, this too leads to the std::terminate() being called.

4. In listing 2.5's `g()` — `std::thread t(some_other_function,42); return t;` — does returning the named local `t` require an explicit `std::move(t)`, or does the compiler handle it automatically? Why (or why not), given `t` is a named variable, not a temporary?
Answer (med): using explicit move to return a local value from a function is an exception - the default or common behavior for returning a value is that compiler considers the return value moveable and automatically moves it to the caller. so this is why return value doesnt need std::move even though its not a temporary or rvalue.

**Correction - 2026-10-05**
If a local variable was instantiated and associated to an lvalue in a function's scope and then is returned, i.e, t object was instantiated in a function and was returned like return t, then the compiler considers it a special case and automatically assumes the object to be movable and so the default return behavior for objects declared inside function is move. 

This is however different from if in that same function, the t object was to be passed to another function and we didnt intend on copying it, then we'd have to use std::move to explicitly dissociate the internals of that object from caller scope's lvalue and re-associate that rvalue to the called function's lvalue.

## Build task — `scoped_thread`

Derived directly in-text (listing 2.6), and a genuine improvement over 2.1's `thread_guard`: takes ownership of the thread by value instead of holding a reference, which removes the "guard outlives the referenced thread" hazard entirely, and moves the joinability check from the destructor to the constructor.

Interface:

```cpp
class scoped_thread {
public:
    explicit scoped_thread(std::thread t);
    ~scoped_thread();

    scoped_thread(scoped_thread const&) = delete;
    scoped_thread& operator=(scoped_thread const&) = delete;
};
```

Contract: takes ownership of the passed-in `std::thread` (moved in, not copied — `std::thread` can't be copied anyway). Throws at *construction time* if the passed-in thread isn't joinable (unlike `thread_guard`, which only checked at destruction). Destructor joins the owned thread. Never copyable.

Edge cases to watch:
1. The book's own `~scoped_thread()` in listing 2.6 joins unconditionally — no `joinable()` check, unlike `thread_guard`'s destructor. Is that actually safe, given `scoped_thread` fully owns the thread privately with no way for outside code to reach in and join/detach it after construction? Work out whether this is a deliberate design simplification or a latent bug — don't just assume the book's listing is automatically correct because it's in the book.
2. Confirm what happens if you construct a `scoped_thread` from an already non-joinable `std::thread` (default-constructed, or already moved-from) — the exception should actually fire.
3. Try putting several `scoped_thread` instances into a `std::vector<scoped_thread>`, the way listing 2.7 does with `std::vector<std::thread>`. Does it compile as written? If not, work out why — think about what declaring a destructor and deleting the copy operations (but not declaring anything about moves) actually does to the compiler-generated move constructor. What would you need to change to make a resizable container of these work, if anything?

Driver: exercise the exception-on-non-joinable case, normal-path join-on-destruction, and the `std::vector<scoped_thread>` scenario from edge case 3 (this doubles as your exercise of listing 2.7's "container of threads" pattern — no separate build task needed for that, it's covered here). Not a concurrency stress test in the TSAN sense, same as `thread_guard` — this is about ownership and lifetime correctness, not data races.

Self-explanation before review: 2-4 sentences, closed-book — why does owning the thread by value (instead of referencing it, as `thread_guard` did) eliminate an entire category of misuse that was possible with `thread_guard`?

Answer : with thread guard, if somehow the thread guard had survived the caller scope - the thread object referred into the thread guard would still not have survived since std::terminate() would have been called which would lead to problems. Another failiure mode comes to mind where the thread object whose reference thread guard carries is also associated to the thread and could call join or detach outside the guard which is although handled in the destructor but still defeats the point. Since scoped_thread takes ownership of the thread entirely, it is the only one who can join the thread so it surviving a scope doesnt cause problems and nor does the other problem occur of some other reference being bale to join the underlying thread.

## Review

Warm-up 0a: correct, second clean hit on the misconceptions log entry (2/3, one more spaced hit before it retires). One precision note: `std::terminate()` doesn't just "take away that thread's resources" — it ends the *entire program*. Worth keeping that framing sharp since it's the actual stakes of the bug.

Warm-up 0b: not answered — skipped over, not a big deal, logged to Outstanding Items rather than repeated here.

Quiz — 3 solid, 1 underdeveloped, 1 real miss:

1. Directionally right (owns a real OS thread of execution, that's why it's not copyable) but the "why not" reasoning is circular — "it's not copyable because the copy constructor is deleted" restates the mechanism without explaining the design rationale. The actual reason, matching `unique_ptr`'s logic exactly: exactly one object should be responsible for managing a given thread of execution at a time; allowing copies would mean two objects both able to join/detach the same underlying thread, which breaks that invariant. Marked "high" — tighten this one.
2. Correct, and demonstrated with good code examples. Could've been "high," not "med" — solid.
3. Correct, and this was the harder part of the question (connecting assignment-onto-an-active-thread to the same underlying rule as destruction-of-an-active-thread) — you got the "same rule, different spot" framing right.
4. **Miss, and this is the actual point of the question.** You concluded `std::move(t)` is needed for `return t;` — it isn't, and the book's listing 2.5 shows exactly that (`return t;`, no `std::move`). Returning a named local variable by value from the function where it's declared is a special case: the compiler treats it as movable automatically (sometimes eliding the move entirely via NRVO), regardless of it being a named/lvalue variable. This is genuinely different from passing a named local as a function *argument*, where you would need explicit `std::move()` — that's likely where the reasoning crossed wires. Marked "high" — recalibrate, this is a classic interview trap specifically because the "named variable needs std::move" rule people learn isn't universal.

Build — `scoped_thread`:

Real improvements over last time: `explicit` correctly included from the start, and — verified by compiling two TUs against the header and linking them — no ODR issue this time, because everything's defined inside the class body (implicitly inline), sidestepping the exact mistake from `thread_guard`. Good generalization of that lesson rather than just patching the one instance.

One thing worth a direct answer rather than leaving it ambiguous: your destructor keeps a `joinable()` check that the book's listing 2.6 omits. That's not wrong at runtime — verified it's harmless — but edge case 1 was asking specifically whether it's *necessary*, and the answer is no: since `scoped_thread` never exposes `t` externally, nothing outside the class can join/detach/move it after construction, so `joinable()` is guaranteed true at destruction time given the constructor's invariant. The book's unconditional `t.join()` is safe *because of* that encapsulation, not by accident. Keeping the check anyway isn't a bug, just worth knowing it's redundant here specifically (unlike `thread_guard`, where the check was load-bearing since external code could always reach the referenced thread).

`non_joinable_test` and `normal_test`: both correct, both with real assertions, good.

`std_vector_container_test`: diagnosis is exactly right — confirmed independently via `static_assert(!std::is_move_constructible_v<scoped_thread>)`, which holds. You stopped at "a solution doesn't come to mind," which is a fair place to stop and flag rather than guess. One nudge, not the answer: your diagnosis already names what's missing. What member function would you need to add, and given `scoped_thread`'s only member is a `std::thread` — which is itself movable — what should that function's body actually do? Worth another attempt before moving on; logged to Outstanding Items either way so it doesn't get lost.

Self-explanation: submitted, and the core distinction (exclusive ownership vs. a reference anyone else can still act on) is right. The first point's phrasing is a little tangled — the mechanism is: if `thread_guard` outlives the *scope* of the thread it references (e.g., the referenced `std::thread` is declared in a narrower block), that thread's own destructor runs first, and since `thread_guard` hasn't joined it yet, it's still joinable at that point — so `std::terminate()` fires immediately, before `thread_guard`'s destructor even gets a turn. Not wrong, just tighten the causal chain when explaining it out loud. Second point (someone else calling `join()`/`detach()` behind the guard's back) is exactly right and is the sharper of the two arguments.

## Round 2 — quiz corrections + move semantics added to the build

Quiz: Q1 and Q4 both fixed correctly. Q1 now states the actual invariant (exactly one manager per thread of execution, matching `unique_ptr`'s logic) instead of restating the deleted-constructor mechanism. Q4 now correctly concludes `std::move(t)` is *not* needed for `return t;`, framed as the special-case exception it is. **Quiz passable.**

Destructor comment ("I know this check is superfluous but I'll keep it anyway") — this is exactly the reasoning edge case 1 was asking for, done correctly and stated explicitly rather than just silently copying the book. Good.

Move constructor/assignment added in response to the `vector<scoped_thread>` nudge — good instinct on the shape (move the `std::thread` member), but there's a real bug in the move assignment operator, not a peripheral one this time — a logic error in code you wrote, same category as `thread_guard`'s missed `explicit`/`inline`. Reviewing it rather than fixing it, per the framework's own distinction between "correctly diagnosed, missing a fact" (explain directly) and "an actual bug in submitted code" (flag it, let it get fixed):

```cpp
scoped_thread& operator=(scoped_thread&& other)
{
    if (this != &other) {
        if (t.joinable()) {
            t.join();
            t = std::move(other.t);   // <-- this line is nested inside the wrong if
        }
    }
    return *this;
}
```

`t = std::move(other.t)` only runs when `t.joinable()` (the *destination's* current state) is true. If `this->t` happens to be non-joinable at the time of assignment — already moved-from, or previously joined — the transfer is skipped entirely, silently. `other`'s thread is never actually moved out, `this` stays empty. Confirmed with a real repro: assigning into a `scoped_thread` whose `t` was already non-joinable, then letting the source go out of scope — the source's own destructor ends up doing the join (because it still secretly owns the thread), not the destination's, proving the transfer never happened. The `joinable()` check belongs *only* around the `t.join()` call (don't abandon whatever `this` currently owns) — the actual move (`t = std::move(other.t)`) needs to run unconditionally below it, not nested inside that check.

Also: `std_vector_container_test` still isn't exercising anything — it's an empty vector with the `push_back` loop still commented out, and the comment above it ("Cannot compile this") is now stale since the class compiles into a vector fine as of the move-ctor addition. This edge case isn't closed yet: needs the loop uncommented, and ideally a scenario that actually exercises move-*assignment* specifically (e.g. `erase()` on a middle element, or `std::sort`) rather than just `push_back`-driven reallocation, since reallocation alone would mostly exercise the move constructor (which is correct) and might not even surface the assignment bug above.

Logged both to Outstanding Items. Not blocking moving to 2.4.

### Correction check (2026-10-06)

These corrections were written after the review, so they don't count as retrieval and the queue schedule is unchanged. The queue items' `source` lines now point here. Q1 and Q4: correct, including the contrast in Q4 (a named local passed *to* another function by value needs `std::move`). Still no written correction for the two later 2.3 warm-up misses (self-move-assignment, asked in 3.2.4; the `get_thread()` accessor, asked in 4.2.1). That's optional: `2.3-self-move-assign` and `2.3-scoped-thread-exclusive-ownership` cover them.
