# 2.4 — Choosing the number of threads at runtime

Session 5.

Warm-ups (2 — priority: misconceptions log, then whatever's due):

0a. (Misconceptions log, 2/3 — needs this one clean to retire. Different scenario this time, not a rephrase of the last two.) You're reviewing a colleague's code: a class has a `std::thread` data member, and the class defines no destructor of its own (so the compiler-generated one runs). No `join()` or `detach()` anywhere in sight. What happens when an instance of that class is destroyed while its thread is still running its task? Separately — what would you expect instead if that same thread had already been `detach()`-ed earlier, before this destruction happens?

Answer: if the class is destroyed and the thread is neither detached not joined before the destruction of the class instance, then std::terminate() is called and the thread resources are abruptly taken away without any graceful wrap-up of the task it was doing no matter where it was in doing that task. 
Now if the thread object had been detached before the class instance was destroyed, then that is less of a cause for concern since the object has no more association with the underlying system thread which is now owned and managed by the C++ runtime. So, if the destructor is responsible for the cleanup then it should a joinable() check before running join or detach on its member thread so as to avoid calling these on a dissociated thread object could end up causing a std::system_error.

0b. (2.1's due item from session 4 — never got answered, re-asking as-is rather than skipping it again.) When you pass a callable object to `std::thread`, why does the book emphasize that "the copy must behave equivalently to the original"? Give a concrete way that could fail in practice.

Answer: Ok so this statement cautions a thread user when passing callables like functors (structs with operator() overloads) that rely on internal states or members that may be pointers, references or synchronization objects. passing callables to threads uses copy constructors of the callable objects and if copying causes changes in callable behavior due to bad design or lack of foresight in terms of multithreading - then we get the failure the book warns about. suppose you have following callable struct / functor:

```cpp

struct functor{
    int* data;

public:
    functor(int* p) : data(p){};

    functor(const &functor other){
        // this will create a seperate copy on the heap and
        // dissociate from the original data on the heap and
        // thus the copy will not allow the desired side-effect 
        // on the callable which we might be expecting.
        data = new int(*other.data);
    }

    void operator()(){
        ++(*data);
    }

}

```
so if we passed the above callable to a thread, its copy will increment its own data copy in the heap and never the actual callable's data and thats obviously not what one desires when giving a thread only the callable with no other way to get the result back unless we start with async or promises or futures and stuff which allows for receiving data more naturally.

Before the build task — a framing question, not a formal warm-up, just worth answering in your head before you start: every build task so far (`threadsafe_queue`, `thread_guard`, `scoped_thread`) needed a mutex or a condition variable somewhere. `parallel_accumulate` (below) has multiple threads writing into a shared `std::vector<T> results`, and needs none. Why not, before you read the spec?

Answer: we havent implemented a thread safe queue yet - thats one, and thread_guard and scoped_thread dont need either mutex or condition variables if u look at the implementations so far. Now to answer your question - mutexes and synchronization primitives like condition variables are tools to allow for communication between threads over a shared data structure or memory without corrupting it by simultaneous alterations which are also called race conditions. the build task of parallel accumulate doesnt need these because the threads arent sharing the same data, they are merely accumulating seperate sections of an array into a seperate indexes of a results array which then one thread accumulates at the end so they are working in isolation with no need of communication among them.

## Quiz — closed-book, tag each answer's confidence (low/med/high) before checking against the book

Recall:
1. Compared to serial `std::accumulate`, `parallel_accumulate` adds two requirements on its template parameters — one on the iterator category, one on `T`. What are they, and why does parallelizing specifically demand them when the serial version doesn't?

Anwer (med): Im thinking this is because of template argument deduction, i.e., the compiler infers the template parameters from the function arguments. so from the begin() and end() iterators we give it - it can infer type std::vector::iterator and from result it can glean the return type:
```cpp
template<class InputIt, class T>
T accumulate(InputIt first, InputIt last, T init);
```
where as in the parallel accumulate_block callable, we had to define our own callable because as we know - we cant get back a result from a thread like we can in a function call so we had to make the callable where we could store the result as a data member and we made it simple with explicit type definitions to keep things simple in the code rather than delving into esoterics like type deductions.

**Correction - (2026-10-05)**
The two actual additional requirements need to be honored for the parallel_accumulate to work:

The Iterator must be at least a forward iterator, not just an input iterator. std::accumulate makes one single pass and never needs to revisit a position, so an input iterator (single-pass, can't be copied and re-read) is enough. parallel_accumulate needs std::distance(first, last) to size the problem, and then needs to walk the range again — independently, in pieces, handing each block's start/end to a different thread — to actually split it. That requires the multipass guarantee forward iterators provide (a copy of an iterator can be dereferenced and advanced independently of the original); input iterators don't promise that a second pass over the same range is even valid.

T must be default-constructible. std::accumulate only ever copies/moves init and the results of operator+ — it never needs to conjure a T out of nothing. parallel_accumulate does: std::vector<T> results(num_spawn_threads), i.e., we do need ability to define instances of type T without having to give their constructor values. parallel_accumulate does default-constructs num_spawn_threads many Ts before any thread writes into them. If T has no default constructor, that line doesn't compile.

2. `parallel_accumulate` can produce a different numeric result than `std::accumulate` on the exact same input, even though both are "computing a sum." Why? What category of `T` is this a real, practical concern for (not just theoretical)?

Answer (med): with the way computers internally handle floating point - the order in which operations occur on floating point numbers, i.e., on types like double or float, we dont have the associative property upheld anymore. a std::accumulate is a simple loop which goes from one end to the other, depending on the ends u specify, however, when we section the whole vector among several threads that then work across different sections with their own separate relative ordering and we are bound to have some small differences in the result as compared to synchronous accumulate. the type T is a real concern because this applies to types like float or double where the computer's internal handling causes invalidation of associative property of operations.

## Build task — `parallel_accumulate`

Derived directly in-text (listing 2.8) — a full worked parallel algorithm, not just a data structure. Genuinely reusable: this divide-the-range/spawn-N-1-threads/do-the-last-block-yourself/join/combine pattern is the template for "embarrassingly parallel" work in general.

Interface:

```cpp
template<typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init);
```

Contract: computes the same logical result as `std::accumulate(first, last, init)` (modulo floating-point reassociation — see quiz Q2), splitting the work across multiple threads. Must not spawn more threads than a sensible minimum-work-per-thread threshold justifies for small ranges. Must not oversubscribe past `std::thread::hardware_concurrency()` for large ranges. Must correctly handle a range that doesn't divide evenly across threads (no elements dropped or double-counted). Must handle `hardware_concurrency()` returning 0 with a sane fallback.

Edge cases to watch:
1. A range with fewer elements than your minimum-per-thread threshold — confirm you don't launch a wasteful number of threads for a trivial amount of work.
2. A range that doesn't divide evenly by the number of threads chosen — confirm nothing at the boundary gets lost or summed twice.
3. `hardware_concurrency()` returning 0 can't be forced from a normal test — reason about it in the self-explanation instead: what does your code do in that case, and is the fallback value defensible?

Driver: this isn't a TSAN-hunting exercise the way the earlier ones were — there's no shared mutable state between threads here (that's the point, see the framing question above), so a clean TSAN run mostly just confirms you didn't accidentally introduce one (e.g., overlapping block boundaries writing to the same slot). Still run it under TSAN as a sanity check. What actually matters: correctness against `std::accumulate` across a range of sizes — empty, 1 element, fewer than your per-thread minimum, exactly divisible, not evenly divisible, and large enough to actually spawn multiple threads — with real assertions comparing results, not just "didn't crash." Optional and worth a look, not an assertion (since they might coincidentally match): run it once on a `float` or `double` range and compare against `std::accumulate` — see quiz Q2.

Self-explanation before review: 2-4 sentences, closed-book — why does this build task need no mutex or condition variable at all, when every previous one did? What specifically about how `results` is written to makes it safe without one?

Answer: as already explained in the above answers - mutexes and synchronization primitives like condition variables are tools to allow for communication between threads over a shared data structure or memory without corrupting it by simultaneous alterations which are also called race conditions. the build task of parallel accumulate doesnt need these because the threads arent sharing the same data, they are merely accumulating seperate sections of an array into a seperate indexes of a results array which then one thread accumulates at the end so they are working in isolation with no need of communication among them.

## Review

**Verdict: not fully passable yet — one quiz miss (Q1) needs a correction round.** Everything else is solid; this is close.

Warm-up 0a — correct. Cleanly separates the `std::terminate()` case (destroyed while still joinable) from the detached case, and correctly reasons that a destructor doing cleanup should `joinable()`-check before acting. This is the 3rd clean spaced hit on this misconception (sessions 3, 4, 5) — **retired from the misconceptions log as solid**, no more targeted warm-ups needed for it.

Warm-up 0b — correct, and a good concrete example (a functor whose copy ctor deep-copies a heap pointer, so the thread's copy mutates its own detached state instead of the caller's). One syntax slip worth fixing muscle memory on: `functor(const &functor other)` isn't valid C++ — the reference binds to the type, not the other way around. It's `functor(const functor& other)`. Doesn't affect the conceptual answer. This closes out the 2.1 outstanding item from session 4.

Framing question — correct. Threads write to disjoint indices of `results`, never touch each other's slot, and there's no read of shared state until the single-threaded combine at the end — so there's nothing for a mutex to protect.

Quiz Q2 — correct. Reassociation under parallel decomposition, real concern for `float`/`double`, not integral types.

Quiz Q1 — **miss.** The answer addresses template argument deduction (how the compiler infers `Iterator`/`T` from the call), which wasn't what was asked — `std::accumulate` deduces its template parameters exactly the same way, so that can't be the thing parallelizing specifically adds. The two actual requirements:

1. **`Iterator` must be at least a forward iterator, not just an input iterator.** `std::accumulate` makes one single pass and never needs to revisit a position, so an input iterator (single-pass, can't be copied and re-read) is enough. `parallel_accumulate` needs `std::distance(first, last)` to size the problem, and then needs to walk the range again — independently, in pieces, handing each block's `start`/`end` to a different thread — to actually split it. That requires the multipass guarantee forward iterators provide (a copy of an iterator can be dereferenced and advanced independently of the original); input iterators don't promise that a second pass over the same range is even valid.
2. **`T` must be default-constructible.** `std::accumulate` only ever copies/moves `init` and the results of `operator+` — it never needs to conjure a `T` out of nothing. `parallel_accumulate` does: `std::vector<T> results(num_spawn_threads);` default-constructs `num_spawn_threads` many `T`s before any thread writes into them. If `T` has no default constructor, that line doesn't compile.

Worth a short correction in your own words when convenient — doesn't need to be elaborate, just enough to show the distinction registers, then we can close 2.4 out.

Build (`parallel_accumulate`) — compiles clean and passes every assertion (empty, 300, 1, 30 <-min, 939 not-evenly-divisible, verified by actually compiling and running it, not just reading it). Block partitioning is correct — no dropped or double-counted elements at boundaries (confirmed by the 939-element case). `hardware_concurrency() == 0` fallback to 2 is sane and matches the book's own suggestion.

One real deviation worth understanding, not a bug: `max_threads = length / min_elements` is plain floor division; the book uses ceiling division, `(length + min_per_thread - 1) / min_per_thread`. Floor division means any range smaller than `min_elements` (your 30-element test case, with `min_elements = 50`) gives `max_threads == 0`, which is why the `if (num_spawn_threads == 0) return std::accumulate(...)` branch exists — it's a special case patching over a gap the formula creates. Ceiling division never produces 0 for a nonempty range, so the book's version doesn't need that early-return branch at all — a single-thread case falls out naturally as `num_threads == 1`, `num_spawn_threads - 1 == 0` worker threads spawned, and the "remaining elements handled by the caller" path does the whole range serially anyway. Same end behavior for your test cases, but floor division also means anything under `2 * min_elements` (up to 99 elements here) runs fully single-threaded even on a many-core machine, where the book's formula would already be splitting across 2+ threads by ~50 elements. Not wrong, not required to fix — just worth knowing which formula you chose and why it changes the threshold.

Self-explanation — correct and matches the framing answer; sufficiently specific about *why* (disjoint indices, no shared writes) rather than just restating "no shared state."

Float/double case — your "no difference observed" is a fine, honest result, not a red flag. At `double` precision, summing ~300 sequential values with a magnitude around 300 doesn't accumulate enough rounding error to be visible without going out several more decimal digits than default `std::cout` precision shows, or without a much larger/more disparate-magnitude range. Not required to chase further — Q2's reasoning is what mattered here, and that's demonstrated correctly regardless of what this particular run showed.

### Correction check (2026-10-06)

These corrections were written after the review, so they don't count as retrieval and the queue schedule is unchanged. The queue items' `source` lines now point here. Q1: correct. Forward iterator (multi-pass, blocks walked independently) and default-constructible `T` (`std::vector<T> results(n)`).
