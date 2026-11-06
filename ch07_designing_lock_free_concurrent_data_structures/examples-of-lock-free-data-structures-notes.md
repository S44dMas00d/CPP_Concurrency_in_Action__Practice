# 7.2 — Examples of lock-free data structures

## 7.2 intro, 7.2.1 (lock-free stack, Listings 7.2–7.3), 7.2.2 (reclaiming nodes, Listings 7.4–7.5)

```
$ python3 tools/review.py due   (2026-10-20)
Due now: 90 active (89 overdue) | maintenance due: 1 | due in next 3 days: 2 | total active: 92, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.3-sync-needs-value-read  (sec 5.3, 2d overdue, streak 0)
     source: synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(b)(c)
  2. [misconception] 5.2-cas-weak-vs-strong  (sec 5.2, due today, streak 2)
     source: atomic-operations-and-types-notes.md 5.2.3 Q2
  3. [miss] 5.2-atomic-flag-static-init  (sec 5.2, 21d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.2.2 Q1 reached for magic statics instead of constant init / static-init-order fiasco)
Also due (next worksheets): 87 more (5.2-last-one-out-counter, 5.2-rmw-result-not-reload, 5.1-modification-order, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 14 (F1-F14)
Lint: 74 items more than 7 days overdue (review-only paused through ch7, per the 2026-10-16 plan)
```

7.1 is `skipped` (your call: short and advisory).

Triage:
- **7.2 intro:** why the examples start with `seq_cst`; only `std::atomic_flag` is guaranteed not to use locks; on some platforms a lock-based structure is the better choice; identify your requirements and profile. Also the 7.3 stack being lock-free but not wait-free. **Recall** (Q1).
- **Returning the value from `pop()`:** why `pop(T&)` no longer helps; why the data is allocated in `push()` and held by `shared_ptr`; why Listing 7.4 `swap`s the data out rather than copying the pointer. **Recall** (Q2).
- **Chaining onto the pending list** with a plain load and store instead of the CAS loop. **Code-tracing** (Q3).
- **When the counting scheme reclaims and when it can't:** a single popper, pushed nodes that never need tracking, where `delete old_head` sits, and high load. **Recall** (Q4).
- **Listings 7.2–7.5 are a reusable mechanism, and later 7.2 sections build on them, so they're a build:** `lock_free_stack` (below). The push CAS loop, "build the node before publishing it", the pop CAS with its null check, and `try_reclaim`'s claim-then-recheck logic are all covered by the build and its self-explanation, so they aren't quizzed.

**Add a low/med/high confidence tag to every answer.** The last two worksheets had none.

### Warm-up

1. The risk thread publishes new limits for the strategy thread.

```cpp
struct limits { int max_pos; int max_notional; };
limits lim;                                // plain struct, not atomic
std::atomic<bool> lim_ready{false};

// risk thread
lim.max_pos = 50;
lim.max_notional = 1'000'000;
lim_ready.store(true, std::memory_order_release);

// strategy thread, version A
if (!lim_ready.load(std::memory_order_acquire)) {
    int p = lim.max_pos;           // (A) "use whatever the old limit was"
}

// strategy thread, version B
int n = lim.max_notional;          // (B1)
while (!lim_ready.load(std::memory_order_acquire)) {}
int p = lim.max_pos;               // (B2)
```

   For each of reads (A), (B1) and (B2): is it ordered after the risk thread's writes to `lim`? Name the relation that's there or missing. Where it's missing, say exactly what the program has. Is "it just reads the old limit" a correct description?

Answer: I'll talk about each thread separately. First thread A, thread A doesnt have a while loop over the lim_ready.load(), this means that for the load to run and catch the change in lim_ready from false to true, the store has to be run exactly before A gets to its load and essentially, for this acquire release pair to synchronize, the stars would need to align, its that unlikely for the timing to match. Assuming the synchronization does happen, which is unlikely, the if statement will be bypassed and p will not read the value 50 on the marked line. If the synchronization doesnt happen which is far more likely due to lack of while loop, the if statement might read true since the store might have already happened far earlier which means the if statement will not allow control to pass; on the other hand if the store() has not yet happened, the if statement will allow control to go into its scope and there p will most definitely read the old value because if due to happens before and how std::memory_order_release works, for p to read the new value 50, the store(true) would have run because all operations above it would have to be visible and vice versa, and since these two operations cannot be visible while both not having run, the comment // (A) "use whatever the old limit was" is accurate.

for thread B1, this operation n = lim.max_notational is not participating in any memory ordering because it comes before not after the acquire_load below; therefore the value it reads from lim.max_notional is purely circumstantial in that it might be the new one, it might be the old one, we don't know, so any ordering here is missing and "it just reads the old limit" does not seem like an apt description. 

for thread B2, the operation p = lim.max_pos is most certainly going to read the latest value stored in lim.max_pos because the while loop above is perfectly setup to synchronize with the store operation in the risk thread such that as soon as the store(true) happens, the while loop will capture that in its load and will exit and then due to happens before setup in the risk thread, lim.max_pos will have the latest value, i.e., 50. So, "it just reads the old limit" is definitely not the statement that describes this situation.


2. A colleague rewrites Listing 7.2's `push()` to use `compare_exchange_strong` "so the loop never wastes an iteration".
   (a) What is a spurious failure of `compare_exchange_weak`? Why can it happen on a machine without a single compare-and-exchange instruction?
   (b) What does `compare_exchange_strong` do internally on such a machine, and what does that make her `push()` loop?
   (c) When does the book say `compare_exchange_strong` is the better choice even inside a retry loop? Is `push()` that case?

Answer: 

(a) in architectures with no single Compare and Swap instruction, there are two instructions:
- Load Linked (LL): read a value and register a watch on the address.
- Store Conditional (SC): write the value only if no other has touched the memory address we are watching.
problem is that in heavy load, the scheduler can interrupt between these two operations such that in a CAS operation, compare_exchange_weak with no internal retry loop, even if the read and compare part succeeds, the write may fail due to this interruption because SC doesnt proceed if another thread wrote to the memory location before it.

(b) `compare_exchange_strong` has an internal retry loop in the case of two commands described above such that on a spurious failure, it retries the CAS operation itself with the same expected and desired values in an internal retry loop leaving the user to only handle the value mismatch and not the spurious failure.

(c) the book recommends using `compare_exchange_strong` when the retry loop's body - which often involves resetting and recalculating the expected and desired values - is expensive to repeat, such as that loop containing atomic operations for example. In that scenario book recommends using `compare_exchange_strong` and the `push()` from listing 7.2 is definitely a match for that.


3. A global `std::atomic_flag connector_busy = ATOMIC_FLAG_INIT;` guards a venue connection. The constructor of another global object, defined in a different `.cpp` file, calls `connect()` during program start-up, and `connect()` spins on `connector_busy.test_and_set()`.
   (a) Is the flag guaranteed to be in its clear state when that constructor runs? What does the book guarantee for a `std::atomic_flag` with static storage duration initialized with `ATOMIC_FLAG_INIT`, and which class of problem does that rule out?
   (b) A teammate says: "C++11 already made statics thread-safe, so that guarantee is nothing special." Which C++11 guarantee from chapter 3 is he thinking of? Why is it a different guarantee?

Answer:

(a) the C++ std needs to guarantee that on every platform, no matter how fringe or exotic, the flag starts in a know state before any thread touches it. the `ATOMIC_FLAG_INIT` is a macro the platform defines to mean exactly "whatever initialization gives a guaranteed CLEAR state on this hardware". Now static duration covers global variables and local variables declared with static keyword. static variable initialization used to be a race condition in older multithreaded programs however since C++11, the C++ standard guarantees no race condition on static initialization in multithreaded code and for static atomic_flag objects, objects are initialized at program startup, before any thread runs - so no thread touches an uninitialized flag.

(b) for the normal static variables get race free initialization on the first call by a callable that holds the static variable - this is the chapter 3 guarantee. However, in the case of atomic flag is that initialization happens even before the first call,i.e., at program startup, which makes it a different and more powerful guarantee.

### Quiz

1. (Recall) The 7.2 introduction and the 7.3 stack.
   (a) Why do the first examples use `memory_order_seq_cst` for every atomic operation?
   (b) Code with no mutexes in it may still not be free of locks. Why? Which atomic type is the only one guaranteed not to use them? On such a platform, what might be more appropriate, and what does the book say you must do before choosing an implementation?
   (c) Listing 7.3 is lock-free but not wait-free. Why, in terms of its loops?

Answer (med):

(a) the use of `memory_order_seq_cst` use is deliberate because it enforces a global order among atomic operations and bby extension their peripherals such that there are fewer possible inter-leavings and therefore correctness is easier to reason about and achieve.

(b) the only atomic type guaranteed to not use internal locks to achieve atomicity is the `std::atomic_flag`. All other types, depending on architecture and specifications of the system, may or may not use internal mutex type locks to achieve atomicity, one can this using the function `is_atomic()`. This being said, the perceived benefit one might think will come from using std::atomic in place of std::mutex might not actually be there for the data type being used if all the major operations for that type use internal locks. the book says that before committing to using atomics or mutexes in a particular application, a profiling analysis ought to be carried out where both ways are implemented in a smaller area or feature and they be commpared accross different metrics and then a decision be made. Atomic ooperations are always more expensive and slower than their normal counter parts because they prevent optimizations through operation reordering, often flush caches to maintain synchronization depending upon the strength of memory order guarantees, and so on. All this to say that they might actually be slowing the code base down on top of making it hard to implement, so author has recommended a profiling done before committing to an approach long term.

(c) an implementation is lock free when we dont use mutexes and this means that the destruction or suspension of one thread does not block the other threads from making progress, i.e., at least one thread is making progress at all times. Wait free implementations are even better in the sense that each thread makes progress and is not constrained by any other and most importantly, each thread completes its intended operation in a fixed number of steps which means there is no thread starvation or busy wait looping happening to any of the threads. the two retry loops wrapping the CAS operations, also known as the CAS loops are what make the listing lock free but not wait free, the reason is that the CAS loops can lead to unbounded waits and thread starvation of a particular thread.


2. (Recall) Returning the value from `pop()`.
   (a) In chapter 3, `pop(T& result)` was an acceptable way to get the value out exception-safely. Why does it give no advantage in the lock-free `pop()`?
   (b) Returning `std::shared_ptr<T>` solves it, but the book doesn't allocate the `T` inside `pop()`. Why not, and where does the allocation go instead? Why can't returning the `shared_ptr` throw?
   (c) In Listing 7.4, `pop()` calls `res.swap(old_head->data)` instead of copying `old_head->data` into `res`. What would copying keep alive, and for how long?

Answer (med):

(a) this is because in the chapter 3 pop() method we had a mutex covering the part where we assigned the popped value to the reference passed inside the function. In the case of the pop() implementation of listing 7.3, we cannot do so, which means that if we do the reference trick here, we risk setting the same value to two references. In this implementation, we can only safely copy the value once we are sure that the current thread is the only one with the access to the value and this means that the node has been popped and is local to that thread's stack, at which point the whole reference passing trick has no use because the node has been popped already, at that point returning by value is once again more beneficial. Doing return by value as we know has the same copy / move constructor throwing issue and so we are lead returning the shared pointer for the data as returning shared pointer doesnt throw.

(b) allocation is one of the most expensive things the program does and throwing on allocation in pop() runs the risk of losing that data permanently because as we already know, in pop implementation here where the node has to be popped before its data being allocated to some reference or shared pointer, once the node is popped, if the allocation to shared pointer throws, we lose the data. The fix is to do the allocation in push where the allocation preceeds the insertion of the node and so even if it throws there, we have no broken state of the stack. So now in pop operation, all we do is return the shared pointer to the data held by the popped node and as long as that reference is held, that underlying heap allocation will be kept alive. returning shared pointer doesnt throw because its copy constructor is no-throw guaranteed and copying a pointer to another location is not so resource intensive anyways if one reasons about it.

(c) the purpose of using res.swap(old_head->data) is that when we call swap, the swap exchanges what the shared pointers are pointing to, so the old_head gets null whose reference count was 0 and the res gets the data object whose reference count remains 1. Now with that said, if we were to not do this and instead just copy a shared pointer to res, the reference count of the data object held by old_head would rise to 2, even when the old_head is added to the deletion nodes linked list, this count remains 2. This means that the data object being managed on the heap will continue to be managed even if the res reference is used up and deleted in the actual logic downstread of the pop() call and only when the old_head node in the deletion linked list is deleted will that data object be freed from the heap. obviously this is wasteful and so calling swap ensures that the data object is cleaned up as soon as all the useful references to it are exhausted.

3. (Code-tracing) A colleague "simplifies" Listing 7.5's chaining function:

```cpp
void chain_pending_nodes(node* first, node* last)
{
    last->next = to_be_deleted.load();
    to_be_deleted.store(first);
}
```

   (a) Two threads call it at the same time with different chains. Give an interleaving and say what happens to one thread's nodes. What's the consequence for the program?
   (b) In the book's version, `compare_exchange_weak(last->next, first)` runs in a loop. What does a failed attempt do to `last->next`, and why is that exactly what's needed?

Answer (med): 

(a) `std::atomic<node* > to_be_deleted` is the head of the nodes slated for deletion. since many threads can enter the pop() function and consequently, the try_reclaim() function, this means that just because we called the line:

`node* nodes_to_delete = to_be_deleted.exchange(nullptr);`

doesn't mean that the other thread that has called the try_reclaim() function with its old_head node, can't reload its node onto the briefly empty to_be_deleted Linked list and its most probable that it will check and find that there are more than 2 callers in pop() which forces it along the else branch of try_reclaim() and thus calling the above function.

So lets assume that we have threads A and B, A finds out that after it passed the gate `if(threads_in_pop == 1)` B had also entered and it couldnt clear the `if(!--threads_in_pop)` check and has to call `chain_pending_nodes()` where as thread B will then enter and fail `if(threads_in_pop == 1)` since now there are two and then from the else branch it will call `chain_pending_node()` function with its single node. so we now get two concurrent and potentially racing calls to the function in the question - `void chain_pending_nodes(node* first, node* last)` and that means that if there is not a CAS loop, i.e., a while loop to make sure that both the old list nodes and the new node end up on the Linked List headed by to_be_deleted atomic node pointer, then the consequence is that one of these insertions will fail and we will either lose the latest old_head node or lose the previous nodes in this linked list and thereby losing the ability for proper cleanup and thus causing a memory leak.

(b) this line `while (!to_be_deleted.compare_exchange_weak(last->next, first))` is doing plenty of heavy lifting, essentially what we are trying to do with this line is that we are passing into the function a linked list whose first and last nodes we know and what we are trying to do in plain english is that we are trying to assign, to the last node's next of our passed in linked list, the previously situated linked list nodes in to_be_deleted linked list, such that the previously present nodes are at the tail of the one we passed and the head of our newly passed linked list is to be promoted to the head of the whole to_be_deleted linked list by being stored in the atomic node* variable - to_be_deleted. Now in case of a success we see that we expected that last->next is equal to the head held in to_be_deleted and so it just promotes first to be the new head, i.e., to_be_deleted. On the failed case where we expected wrong that the last->next is going to be equal to to_be_deleted, the CAS retry loop updates the value of last->next to exactly what we need, i.e., the to_be_deleted node and then we retry and succeed in promoting first to be the new to_be_deleted head node while the former node of to_be_deleted is demoted to being last->next, making the to_be_deleted linked list complete.

4. (Recall) When the counting scheme works.
   (a) If only one thread ever calls `pop()` on a stack, why can it delete the node straight away? Why does the scheme never need to track nodes that are inside `push()`?
   (b) In `try_reclaim()`, the "did the count reach zero?" test and the pending-list deletion both come before `delete old_head`. Why does the book want that order? Explain it in terms of the window between first seeing `threads_in_pop == 1` and deleting.
   (c) Under what kind of load does this scheme stop reclaiming, and what happens to the pending list then? What does the book say any replacement mechanism has to be able to identify?

Answer (med): 

(a) first, the push() part of the question. the scheme doesnt need to track callers in the push() function because most of the operations regarding creation of a node such as allocating the data on the heap and then setting that `new_node`'s next as the current head, are operations which are happening to the node local stack frame of the push caller, so for this far, no other node can see the new_node and all the while the CAS loop tries to get the head to shift over to this new_node, the other threads can only see it right when the CAS operation, to make the new node the head, succeeds and the loop exits and so does the push() function. All this to say that push never actually overlaps with pop() functionality or any other  for that matter, once its done loading the node on the stack's linked list, its done and it exits immediately. 

second, we have the one thread in pop() part of the question. the reason the old_head node can be deleted immediately if it clears the gate `if (threads_in_pop == 1)` is because in that branch of, once the call to try_reclaim(old_head) had been made, then no other thread can see the old_head node because after the CAS loop `while(old_head && !head.compare_exchange_weak(old_head, old_head->next))` ran for this pop() caller thread  which is now in the if branch of `try_reclaim(old_head)`, the old_head node became local to this thread and that means - no other thread can see old_head and so it can be deleted immediately in the `if (threads_in_pop == 1)` branch of `try_reclaim(old_head)` without worry of crash due to dereferencing of a dangling pointer.

(b) to hit this question a bit more directly we must look at two threads A and B calling pop() simultaneously and both are calling this code:

```cpp
std::shared_ptr<T> pop()
{
   ++threads_in_pop;
   node* old_head=head.load();
   while(old_head && !head.compare_exchange_weak(old_head,old_head->next));
   std::shared_ptr<T> res;
   if(old_head)
   {
      res.swap(old_head->data);
   }
   try_reclaim(old_head);
   return res;
}
```
but it so happens that when B has called pop() and reached the CAS loop, thread A has already called and cleared the first gate of the try_reclaim() function, i.e., `if (threads_in_pop == 1)`. so this means that both A and B could have holding the same old_head from the same head.load() and while A succeeded, B is about to call `old_head->next` in its CAS loop and hitting a crash due to dereferencing a dangling pointer, but this would only happen if A's `delete old_head` ran before B's CAS loop ran for the second time and so to avoid deleting a node in such a situation, the try_reclaim() function has a secondary check to ensure that no other threads have acquired reference to a node slated for deletion in the window between us acquiring the deletion nodes linked list in to_be_deleted and us calling delete_nodes(). with this thought chain, the rest of the checks make sense, passing the gate `if (!--threads_in_pop)` means there are no more threads in pop() that could be holding old_head pointer, not passing that and passing `else if(nodes_to_delete)` means there could be threads holding references to node slated for deletion and so we need to replace them in the to_be_deleted node list. finally we get to the delete command for old_head by which time we know that old_head will have been local to this one thread call of try_reclaim() function and so we can delete old_head.

(c) for this mechanism to work correctly and free the to_be_deleted nodes list, the mechanism needs the try_reclaim caller thread to be the only thread active in pop(), i.e., it needs to pass `if (threads_in_pop == 1)` gate and `if (!--threads_in_pop)` gate. in heavy loads where too many threads are constantly pushing to and more particularly, popping() from the stack such that there is never any such quiescent state coming for so long in the operation of the stack that the system runs out of memory for the to_be_deleted nodes list, then that is the exact scenario where this mechanism fails. The book uses this to lead into hazard pointers which allow for checking exactly which nodes each threads is currently accessing - rather than counting the threads in pop which might never go to zero.

### Build task — `lock_free_stack` (Listings 7.2–7.5)

Folder: `ch07_designing_lock_free_concurrent_data_structures/lock_free_stack/` (scaffolded: `CMakeLists.txt`, the root `add_subdirectory` line, and empty header and driver). Book closed.

Interface:

```cpp
template<typename T>
class lock_free_stack {
public:
    lock_free_stack();
    ~lock_free_stack();
    lock_free_stack(const lock_free_stack&) = delete;
    lock_free_stack& operator=(const lock_free_stack&) = delete;

    void push(T const& data);
    std::shared_ptr<T> pop();     // nullptr when the stack is empty
};
```

Private helpers are up to you; the book uses `try_reclaim`, `delete_nodes` and two `chain_pending_nodes` overloads, plus `chain_pending_node`.

Thread-safety contract:
- **No locks:** no `std::mutex` and no spinlock. The shared state is three atomics: `head` (`std::atomic<node*>`), `threads_in_pop` (an atomic counter) and `to_be_deleted` (`std::atomic<node*>`, the pending list). Use the default `memory_order_seq_cst` for everything, as the book does at this point.
- A node holds `std::shared_ptr<T> data` and a plain `node* next`. **The node, including its data, is fully built before it becomes reachable from `head`.** It isn't modified after that.
- `push()` and `pop()` update `head` with `compare_exchange_weak` in a loop, relying on a failed exchange to refresh the expected value. `pop()` never dereferences a null `head`, and only the thread whose exchange succeeds returns a given node's data.
- `pop()` moves the data out of the node, so the node no longer owns it once popped.
- **Reclamation:** a popped node is deleted only when no other thread can still hold a pointer to it. Otherwise it goes on the pending list. The pending list is reclaimed when no other thread is in `pop()`. Chaining onto the pending list never loses a node, even when several threads chain at once.
- The destructor (never called concurrently with anything) frees every node still on the stack and every node on the pending list. **The review runs your driver under LeakSanitizer.** To check it yourself: an ASan build run with `ASAN_OPTIONS=detect_leaks=1`.
- `push()` and `pop()` are safe to call concurrently from any number of threads. The structure is lock-free, not wait-free.

Tests to write:
1. **Every value comes out exactly once under contention.** 4 pusher threads each push 20,000 distinct ints (pusher `p` pushes `p*20000 + k`). At the same time, 4 popper threads each pop in a loop, retrying on `nullptr`, until a shared atomic count of successful pops reaches 80,000. Assert every one of the 80,000 values was received exactly once, across all poppers. *Catches:* lost or duplicated nodes from a wrong CAS loop, and use-after-free in reclamation (ASan or TSAN flags it).
2. **One item, many poppers: exactly one winner.** For 500 rounds, push one value (the round number), then release 4 popper threads together, each calling `pop()` once. Reusing the same 4 threads across rounds is fine. Assert exactly one thread got a non-null result, it equals the round number, and the other three got `nullptr`. *Catches:* a `pop()` that returns a node without winning the exchange (two threads return the same node), and dereferencing a null `head` on an empty stack.
3. **A throwing push leaves the stack unchanged.** Use a type whose copy constructor throws while a global flag is set. Push 1 and 2 with the flag off, then turn it on. Assert that `push(3)` throws. Turn the flag off, then assert `pop()` gives 2, then 1, then `nullptr`. *Catches:* linking a node into the stack before its data has been built.

Self-explanation (2–4 sentences, closed-book, before review): Why does `push()` need the compare/exchange loop, and why must the node be fully built before it? In `try_reclaim()`, why must a thread that saw itself alone in `pop()` check `threads_in_pop` **again after** claiming the pending list, before deleting it? Describe the book's three-thread scenario (Figure 7.1) that breaks if it doesn't.

Answer: push needs the CAS loop because multiple threads can simultaneously call it and what our reference to head.load() which we stored in our new_node->next was a moment ago might not be valid because another caller of push might have changed it with its CAS loop before we ever could, so the CAS loop is used to update the reference in the failed case. As for why the node must be fully built before it, thats because as soon as the CAS loop succeeds and its loaded on to the stack, its all of a sudden visible to every popper thread running on the stack and also the first one in line to get popped too so its only prudent to fully instantiate that node before the popper's CAS loop tries on it and opens us up to dangling pointer dereferencing crashes.

In `try_reclaim()`, why must a thread that saw itself alone in `pop()` check `threads_in_pop` **again after** claiming the pending list, before deleting it? well this is because suppose we took ownership of a to be deleted nodes list in try_reclaim's `nodes_to_delete` variable, after passing the gate `if (num_threads_in_pop == 1)`, we know many interleavings are possible in lock free code, so we can assume that the race window between the gate and the exchage command to take ownership, could have been penetrated by another thread that entered the pop() function and holds a reference to a node say Z node that we in our call to pop() before coming to reclaim had already sent to the to_be_deleted list. if we allow deletion to happen to the deletion list with the Z node in it before the failed CAS comparison with the dereferencing - Z->next - happens, then we are most certain to get a crash and so we must make sure no such pointer hold exists before we run the deletion and thats why the deletion is gated by `if (!--num_threads_in_pop) {`.


### Beyond the book (optional, ungraded)

None this time.

---

## Review: 7.2 intro, 7.2.1, 7.2.2 (2026-10-21)

Confidence tags are on Q1–Q4 (all med), but missing on warm-up 1. Warm-ups 2 and 3 are blank. **The self-explanation is written**, the first time since F11.

### Warm-up

- `5.3-sync-needs-value-read` -> **miss** (the same wrong model as at 6.3).
  > "there p will most definitely read the old value ... the comment // (A) "use whatever the old limit was" is accurate."

  When the load reads `false`, there is no happens-before between the risk thread's writes and read (A). A plain read with no happens-before against a concurrent plain write is a **data race**, so it's **undefined behaviour**, not "the old value". "It just reads the old limit" is wrong for (A) and (B1) alike.
  - (B1): "not participating in any memory ordering because it comes before ... the acquire load" is right. It's sequenced before the load, so it's unordered, and it's again a data race.
  - (B2) is right: the loop exits only on reading `true`, so the release store synchronizes with that load.
  - The "stars need to align / unlikely without a while loop" part is about timing. The question was about ordering, which doesn't depend on timing.
- `5.2-cas-weak-vs-strong` -> **miss** (blank). It also shows in the build: all three retry loops use `compare_exchange_strong` (see Build).
- `5.2-atomic-flag-static-init` -> **miss** (blank).

### Quiz

1. **Hit.**
   - (a) > "enforces a global order among atomic operations ... therefore correctness is easier to reason about"

     Correct.
   - (b) `atomic_flag` is the only type guaranteed not to use locks, and you should profile before choosing. Correct.
     - The function is `is_lock_free()`, not `is_atomic()`.
     - The book's "might be more appropriate" is **a simple lock-based data structure**.
     - "Atomic operations ... often flush caches" isn't in the book. Leave it out of graded answers.
   - (c) > "the CAS loops can lead to unbounded waits"

     Correct. The book's wording is that the loops could in theory loop forever if `compare_exchange_weak()` keeps failing.
2. **Hit.**
   - (a) > "we can only safely copy the value once we are sure that the current thread is the only one ... the node has been popped"

     Correct.
   - (b) Allocate in `push()`, before the node is linked, and returning a `shared_ptr` can't throw. Correct. The book adds that you have to allocate a node in `push()` anyway.
   - (c) Copying keeps the `T` alive until the pending node is finally deleted. Correct.
3. **Hit.**
   - (a) > "one of these insertions will fail and we will either lose the latest old_head node or lose the previous nodes ... causing a memory leak"

     The consequence is right. The concrete interleaving: both threads load the same `to_be_deleted` value X, A stores `first_A`, then B stores `first_B`, overwriting it. A's chain is now unreachable, so those nodes are never deleted: a leak.
   - (b) > "the CAS retry loop updates the value of last->next to exactly what we need"

     Correct.
4. **Miss**, on (b).
   - (a) Correct: a node in `push()` isn't visible until the CAS succeeds, and after a single popper's CAS no other thread can reach the node.
   - (b) You explained what the recheck is for, not why the book puts the count test and pending deletion **before** `delete old_head`. The book's reason: deleting is slow, so test first to keep the window between seeing `threads_in_pop == 1` and claiming/rechecking as small as possible. The longer that window, the more likely another thread has entered `pop()`, and then the pending nodes can't be reclaimed.
     - Also, your scenario has B holding *A's* `old_head`. For that, B must have been in `pop()` when A tested the count, which would have made the count 2, so A would have taken the `else` branch. A's own `old_head` is always safe to delete in that branch.
   - (c) Correct: under constant load there's no quiescent state and the pending list grows without bound. The replacement must identify which nodes threads are accessing (hazard pointers).

### Build

```
lock_free_stack: TSAN 0/10 clean (16 race reports) | ASan+UBSan 7/10 clean | 2-TU link OK
```

Run with `-n 10 -t 8`, because Test 2 hangs (below) and 20 runs at the default 120 s timeout don't fit in a tool call. The 3 ASan "failures" were timeouts, not ASan errors.
- **ASan+UBSan+LSan in scratch** (`detect_leaks=1`, 12 runs): 10 completed, with 0 ASan/UBSan errors and **0 leaks**. The other 2 hung in Test 2. Your destructor frees both the stack and the pending list.
- **Native, 30 runs:** 20 passed, 10 hung after "TEST 1 PASSED".

**Library.** It's a faithful Listings 7.3–7.5. Two of your choices worth noting:
- **Good catch:** an empty `pop()` decrements the counter itself instead of calling `try_reclaim(nullptr)`. In the book's listing, an empty pop that finds other threads in `pop()` would call `chain_pending_node(nullptr)` and dereference null.
- `compare_exchange_strong` in all three retry loops. The contract said `_weak`, and the book prefers weak when you loop on failure anyway (warm-up 2). It's correct, so this is a note, not a fix item.

**The TSAN reports are a data race that's in the book's own design.**
- Every report is the same pair. A popper that loaded a stale `old_head` reads `old_head->next` (header line 93, the CAS's desired value). Meanwhile, the thread that really popped that node writes its `next` in `chain_pending_nodes` to put it on the pending list.
- The node isn't freed, since the reader is counted in `threads_in_pop`, and the stale CAS fails, so the value is discarded. But it's still a plain read and a plain write of the same location, both non-atomic and with no happens-before. **That's the data-race definition from warm-up 1. It doesn't matter that the value read is thrown away.**
- The contract line "the node isn't modified after that [becoming reachable]" is what the book's chaining breaks: it re-uses `next` for the pending list.
- Two fixes, either of which is fine:
  - give the pending list its own link field, so a node's `next` is never written after it's pushed;
  - or make `next` a `std::atomic<node*>`.
- Scratch check of the first fix, with Tests 1 and 3 only: TSAN 0 race reports in 5/5 runs, against reports in 5/5 runs for your header.

**Driver, against the Tests-to-write list.**
- Test 1: written, and its asserts can fail. ✔
- **Test 2 isn't the listed test, and it hangs.**
  - The listed test: 4 poppers each call `pop()` once on the same single item, and the assert is "exactly one non-null result per round, equal to the round number; the other three null". Your version passes a `push_done` flag so that only the popper that wins the flag pops, so poppers never race for the same item. The assert is overall conservation, not one winner per round.
  - The hang: the pusher pushes 499, the count reaches 500, then it waits for `push_done` to be `false`. If round 498's flag hasn't been taken yet, the poppers see 500 and leave, and nobody clears it, so the pusher spins forever. That happened in 10 of 30 native runs.
- Test 3: the throw is asserted ✔, but the two pops' values (2, then 1) aren't. Only the final `nullptr` is checked. That does still catch the listed bug, but the listed asserts are missing.

Self-explanation: written.
- The push CAS reason is right. The node must be fully built because, once `head` points to it, another thread can read it (a data race on a half-built node). It's not about a dangling pointer.
- The recheck scenario needs a **third thread**, as in Figure 7.1. B reads `head` = Y **after** A's count test. Then C pops Y, sees the count above 1, and chains Y onto the pending list. A's exchange then claims Y while B is about to read `Y->next`. In your version the pending node is one "we" sent there earlier, but a node A popped before can't then be picked up by a B that entered after A's test.

Nits (no fix item):
- The helpers (`delete_nodes`, `chain_pending_*`, `try_reclaim`) are public.
- The header includes `<thread>` but doesn't use it.
- `throwing_type(const throwing_type&&)` is a const rvalue reference, so it's never a real move constructor.
- The driver prints debug sizes with `cout`.

### Queue updates

```
python3 tools/review.py log 5.3-sync-needs-value-read miss --note "7.2 warm-up: (B2) right; (B1) 'not participating in any ordering' right; but (A) claimed the read 'will most definitely read the old value' and the comment is accurate -- it's a data race (UB), no happens-before when the load reads false. No 'data race' named anywhere. Same wrong model as 6.3"
python3 tools/review.py log 5.2-cas-weak-vs-strong miss --note "7.2 warm-up: left blank (and his lock_free_stack used compare_exchange_strong in all three retry loops)"
python3 tools/review.py log 5.2-atomic-flag-static-init miss --note "7.2 warm-up: left blank"
python3 tools/review.py add 7.2 7.2-reclaim-window-order "..." --kind miss --source "examples-of-lock-free-data-structures-notes.md 7.2.2 Q4(b)"
python3 tools/review.py add 7.2 7.2-try-reclaim-recheck "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.2 (Listing 7.5 text, Figure 7.1)"
python3 tools/review.py add 7.2 7.2-pop-value-shared-ptr "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.1 Q2"
python3 tools/review.py add 7.2 7.2-lock-free-platform-caveat "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.1 Q1"
```

The concept texts are in `review-queue.csv`, via `review.py list -v --section 7.2`.

Results:
- The three warm-up items are at streak 0 (due 10-23). `5.2-cas-weak-vs-strong` loses its streak of 2.
- The new miss item is due 10-22 and the three core items 10-23.
- 7.2 stays `current`: 7.2.3 onwards remain.

### Fix list

- **Opened F15** (`lock_free_stack`):
  - (a) The library's data race on `node::next`, inherited from the book: a separate pending-list link, or an atomic `next`.
  - (b) Test 2: rewrite it as listed (4 poppers race once per round; exactly one non-null equal to the round, three nulls), with no hang.
  - (c) Test 3: assert the two pops give 2, then 1.

  Re-verify with `verify.sh lock_free_stack` (default 20 runs) and one `detect_leaks=1` run.

### Beyond the book (ungraded)

- **The book's Listing 7.5 has a formal data race.** Re-using `next` to chain the pending list means a stale popper's read of `old_head->next` can overlap another thread's write to it. Under the Standard's definition that's a data race whatever happens to the value, and TSAN flags it every run. The usual fixes are the two in Build.
- **Book slip:** Listing 7.4 calls `try_reclaim(old_head)` even when `old_head` is null, and the `else` branch then dereferences it.

---

## 7.2.3 — Detecting nodes that can't be reclaimed using hazard pointers (Listings 7.6–7.8)

```
$ python3 tools/review.py due   (2026-10-22)
Due now: 89 active (89 overdue) | maintenance due: 1 | due in next 3 days: 8 | total active: 97, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 4.1-notify-outside-lock  (sec 4.1, 1d overdue, streak 2)
     source: waiting-for-an-event-or-condition-notes.md 4.1 Q7 (Correction 2026-10-05; Q7 was mis-graded at the original review)
  2. [misconception] 5.1-data-race-definition  (sec 5.1, 1d overdue, streak 1)
     source: 2026-09-24 migrated from section ledger (prior: 5.1 Q3 gave 2 of 3 conditions (missed 'at least one not atomic'))
  3. [miss] 5.2-last-one-out-counter  (sec 5.2, 23d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.4–5.2.5 Q4
Also due (next worksheets): 86 more (5.2-rmw-result-not-reload, 5.1-modification-order, 5.2-atomic-free-functions, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 15 (F1-F15)
Lint: 80 items more than 7 days overdue (review-only paused through ch7, per the 2026-10-16 plan)
```

Triage:
- **The idea, and setting the hazard pointer** (store it, then re-read `head` until it matches), including the book's caveat about using a pointer's value after the object is deleted. The loop's *why* is in the build's self-explanation. The caveat is **Recall** (Q1).
- **Listing 7.6's details:** `compare_exchange_strong` instead of weak, when the hazard pointer can be cleared, and what happens to nodes that are still hazardous. **Recall** (Q1).
- **Claiming the reclaim list with `exchange()`** before scanning it. **Code-tracing** (Q2), on a version that walks the shared list directly.
- **The cost of the naive scheme, and the better strategies:** wait for 2 × `max_hazard_pointers` pending nodes, and per-thread reclaim lists. **Recall** (Q3).
- **Listings 7.6–7.8 are a reusable mechanism (the book calls hazard pointers "a general-purpose utility"), so they're a build:** `hazard_pointer_stack` (below). Slot claiming, thread-exit release, the type-erased deleter and the hazard-checked delete are covered by the build.
- **Not quizzed:** the IBM patent paragraph. It's context, not a mechanism.
- **F15 is independent of this build.** The hazard-pointer stack never re-uses a node's `next` after it's pushed: the reclaim list has its own wrapper nodes. The 7.2.2 `lock_free_stack` stays as reviewed until you fix F15.

**A low/med/high confidence tag on every answer, warm-ups included.**

### Warm-up

1. The fill handler hands fills to the risk thread through the 4.1-style queue:

```cpp
// fill handler
{
    std::lock_guard<std::mutex> lk(m);
    fills.push(f);
}
cv.notify_one();                                   // after the lock is released

// risk thread
std::unique_lock<std::mutex> lk(m);
cv.wait(lk, [&]{ return !fills.empty(); });
```

   A reviewer says: "Lost-wakeup bug. The risk thread can miss the notification in the gap between the handler unlocking and calling `notify_one()`. Move the notify inside the lock." Is she right? Explain using what `wait(lk, pred)` checks, under which lock, and at which moments. Then say what the book says is actually gained by notifying *after* unlocking.

Answer:


2. Thinking about Listing 7.7, a colleague says: "`hazard_pointer::pointer` doesn't need to be atomic. Only its owning thread ever writes it, so make it a plain `void*`." Other threads read it in `outstanding_hazard_pointers_for()` while the owner stores to it.
   (a) State the definition of a data race with every condition.
   (b) Is her plain-`void*` version a data race? Say which condition decides it.
   (c) In the book's version both accesses are atomic, and `outstanding_hazard_pointers_for()` has no lock and no ordering with the owner's store beyond the atomics themselves. Which condition makes that legal?
   (d) In your F15 race, the stale popper's CAS fails and the value it read from `next` is thrown away. Does that change whether it's a data race? Why?

Answer:


3. Six venue connectors start up in parallel. Each one, once connected, runs:

```cpp
connected.fetch_add(1);
if (connected.load() == 6)
    start_trading();
```

   (a) Can `start_trading()` run twice? Can it run zero times? Give the interleaving for whichever is possible, and the reason for whichever isn't.
   (b) Fix it using the read-modify-write's own result. Why does exactly one thread then see 6?

Answer:


### Quiz

1. (Recall) Listing 7.6 and the hazard-pointer loop.
   (a) The loop that sets the hazard pointer compares a freshly loaded `head` with a pointer that may already have been deleted. What does the book say about using a pointer's *value* after the object has been deleted, and what must you do about it?
   (b) Listing 7.6 uses `compare_exchange_strong()` in the outer loop, unlike Listing 7.3. Give the book's reason.
   (c) At what point in `pop()` can the thread clear its own hazard pointer, and why is that safe?
   (d) After `pop()` calls `delete_nodes_with_no_hazards()`, some nodes may still have outstanding hazard pointers. What happens to them?

Answer (med):

(a) the question is a bit vague but I'll answer what i understand from it, the loop is doing what the book says we should do when the node we are registering in hazard pointer for that thread turns out to have been deleted, we re-read it and re-publish it in the hazard pointer until the old_head and temp pointers match to the current stable head.

(b) the reason is exactly what we learned when discussing the suitability of _weak and _strong variants in which situation. We choose _strong when the work being done in the retry loop is significant and here we can see that the retry loop has a lot of work that we dont want to redo on a spurious failure. To be specific, in the retry loop we have another nested while loop doing three atomic operations and we know atomic operations can be more expensive than their regular counter parts.

(c) the nodes with active or outstanding hazard pointers are requeued to the `nodes_to_reclaim` node list as opposed to being deleted if the function `outstanding_hazard_pointers_for(current->data)` returns true and only then do the nodes get reclaimed.


2. (Code-tracing) A colleague drops the `exchange()` from Listing 7.8:

```cpp
void delete_nodes_with_no_hazards()
{
    data_to_reclaim* current = nodes_to_reclaim.load();
    while (current)
    {
        data_to_reclaim* const next = current->next;
        if (!outstanding_hazard_pointers_for(current->data))
            delete current;                 // still linked into the shared list
        current = next;
    }
}
```

   (a) Two threads finish `pop()` at about the same time and both call this. Give an interleaving that ends in undefined behaviour, and name it.
   (b) A third thread calls `add_to_reclaim_list()` while one of them is walking. What can that do to the walk, or to the list?
   (c) What does the book's `exchange(nullptr)` guarantee, and what are other threads still free to do while one thread reclaims?

Answer (med):

(a) suppose threads A and B call `delete_nodes_with_no_hazards` at roughly the same time, then both get the same pointer to `nodes_to_reclaim` node and both can clear the gate `while (current)` unlike when we had `data_to_reclaim* current = nodes_to_reclaim.exchange(nullptr)` where one of them would for sure end up with nullptr in this interleaving and fail the `while (current)` gate. Anyways, they both are racing through and enter the loop and both end up running delete on the same current node which as we know - two threads calling delete on the same raw pointer is undefined behavior (UB) if both attempt to destroy the same object.

(b) the code for add_to_reclaim_list() is as follows:
```cpp
void add_to_reclaim_list(data_to_reclaim* node){
   node->next = nodes_to_reclaim.load();
   while(!nodes_to_reclaim.compare_exchange_weak(node->next, node));
}
```
by the code one can see that no matter how many threads are walking or changing the list, the CAS loop on the atomic pointer to the head of the linked list is designed to factor in other threads and avoid any UB from happening, i dont see any problem happening to another thread walking the list.

(c) the book's `exchange(nullptr)` guarantees that once it runs then the exchange completes then the node's list at that time, all of its nodes are only now visible to the thread that has completed the exchange command, i.e., only in its stack frame does a reference to that node's list exist and is invisible to other threads. Other threads may push new nodes and form a new linked list under the head atomic pointer  `nodes_to_reclaim`, however, they may no longer interact with the one we got into local scope of the caller under the local `data_to_reclaim* current` variable. this then allows for deletion processes to proceed without risk of other threads interfering.


3. (Recall) Cost, and the better strategies.
   (a) Why is the simple scheme expensive? Count the atomic loads one `pop()` does in `outstanding_hazard_pointers_for()`, for its own node plus k pending nodes. What does the book say about the cost of atomic operations?
   (b) The book's first improvement: don't reclaim until the list is long enough. Why does waiting for `max_hazard_pointers + 1` nodes barely help, while `2 * max_hazard_pointers` does? What does the book work out as the amortized cost per `pop()`?
   (c) What downsides does that scheme still have? What does the second improvement (per-thread reclaim lists) remove, and what does it cost in memory? What happens to a thread's leftover nodes when it exits?

Answer (med): 

(a) Each call to `delete_nodes_with_no_hazards()` scans the entire reclaim list - upto max_hazard_pointers entries - and then for each entry, which once again, can be upto max_hazard_pointers number of entries, it calls `outstanding_hazard_pointers_for()` which scans all the max_hazard_pointers hazard pointer slots. so in the worst case, assuming max_hazard_pointer is 100, for the current node we check `outstanding_hazard_pointers_for()` so 100 checks right there. then for each entry stored in the `hazard_pointers` list, say worst case is 100 entries, we run the same 
call `outstanding_hazard_pointers_for()` so 100*100 checks there. so in all we have num_checks = max_hazard_pointers + (max_hazard_pointers * max_hazard_pointers) = 10,100 atomic operations for one pop(). The book then reasserts that this is a bad tradeoff and makes pop() an expensive operation since an atomic operation can be 100 times slower than non atomic operations and then 10100 of them for each pop in a high load multithreaded environment can lead to a highly stressed system.

(b) the idea is that if the nodes_to_reclaim list contains more than `max_hazard_pointers + 1` nodes then the excess entries are guaranteed to be unprotected since one thread is assumed to be referencing one node, i.e., the way the pop() is structured, one node might be referenced by many threads but its not the other way around, i.e., one thread isnt holding many node references, so naturally we have one to one pairing which means there are stale nodes in there which are no longer guarded by hazard pointers. the book says that unlike the previous `max_hazard_pointers ^ 2` atomic operations per pop() call, the amortized number for this setup where we only run deletion when we have `max_hazard_pointers * 2` nodes in nodes_to_reclaim list, is about `max_hazard_pointers` atomic calls on average.

(c) the downside of the `max_hazard_pointers * 2` scheme is the increased use of memory, i.e., we now have `max_hazard_pointers * 2` nodes alive all the time in the `nodes_to_reclaim` linked list and this makes sense only when we have the memory to spare and CPU time share is more scarce a resource.

the second approach discussed is that each thread has its own local `nodes_to_reclaim` linked list and this means that deletion doesnt require exclusive ownership be taken by the deleter function but rather it just operates on the local list as needed after checking the hazard_pointers list. On top of this we sweep the local lists only when the local list reaches capacity of `max_hazard_pointers`. So the benefit is that there is no contention among the threads on a single linked list for nodes to be deleted and it leads to simpler deletion function logic. the problem it causes is that if a thread exits with nodes still on its local `nodes_to_reclaim` linked list then those have to be shifted over to some global list so that other threads may delete those nodes when they are no longer protected or referenced by the hazard pointer list, this again complicates things.

### Build task — `hazard_pointer_stack` (Listings 7.6–7.8)

Folder: `ch07_designing_lock_free_concurrent_data_structures/hazard_pointer_stack/` (scaffolded: `CMakeLists.txt`, the root `add_subdirectory` line, and three empty files: `include/hazard_pointers.hpp`, `include/hazard_pointer_stack.hpp` and `tests/hazard_pointer_stack_driver.cpp`). Book closed.

You can copy `push()` and the node from your `lock_free_stack`. The stack's header includes `"hazard_pointers.hpp"`.

Interface, `hazard_pointers.hpp` (a general-purpose utility, not tied to the stack):

```cpp
constexpr unsigned max_hazard_pointers = 100;

std::atomic<void*>& get_hazard_pointer_for_current_thread();
bool outstanding_hazard_pointers_for(void* p);
template<typename T> void reclaim_later(T* data);
void delete_nodes_with_no_hazards();
```

Interface, `hazard_pointer_stack.hpp`:

```cpp
template<typename T>
class hazard_pointer_stack {
public:
    hazard_pointer_stack();
    ~hazard_pointer_stack();
    hazard_pointer_stack(const hazard_pointer_stack&) = delete;
    hazard_pointer_stack& operator=(const hazard_pointer_stack&) = delete;

    void push(T const& data);
    std::shared_ptr<T> pop();     // nullptr when the stack is empty
};
```

Thread-safety contract:
- **Header-only rule:** every namespace-scope variable (the slot table, the reclaim-list head) and every non-template namespace-scope function in `hazard_pointers.hpp` must be `inline`. That's C++17 inline variables and inline functions. Without it, two `.cpp` files including the header break the one-definition rule. `verify.sh`'s two-TU check (two `.cpp` files that include the header, linked together) catches it; F3 and F5 were this bug.
- **Slots:** a fixed table of `max_hazard_pointers` entries, each an atomic owner thread id plus an atomic `void*`.
  - A thread claims a free slot with **one compare/exchange** on its id the first time it asks.
  - A per-thread (`thread_local`) owner object caches the slot.
  - If the table is full, the call throws `std::runtime_error`.
  - When the thread exits, its slot's pointer is cleared and the slot is released for reuse.
- `outstanding_hazard_pointers_for(p)` is true if any slot's pointer currently equals `p`.
- **Deferred deletion:** `reclaim_later(p)` records `p` together with a deleter for its real type, so the eventual `delete` runs `T`'s destructor. It never deletes through a `void*`.
  - Adding to the reclaim list never loses an entry when several threads add at once.
  - `delete_nodes_with_no_hazards()` deletes every recorded object that no slot points to, and keeps the rest for a later call. Two threads calling it at the same time never delete the same object twice.
- **`pop()`:** before dereferencing a node pointer it read from `head`, the thread has set its hazard pointer to that node, and has confirmed that `head` still holds the same pointer after setting it. That holds every time it re-reads `head`.
  - It clears its hazard pointer once the node is removed from the stack.
  - It deletes the node at once if no hazard pointer references it, and otherwise hands it to `reclaim_later`.
  - Then it calls `delete_nodes_with_no_hazards()`.
- `push()` is as in 7.2.1: the node is fully built before it's published, and **a node's `next` is never written after it's pushed**.
- The destructor (never concurrent with anything) deletes every node still on the stack, then calls `delete_nodes_with_no_hazards()` once.
- Default `memory_order_seq_cst` throughout. `push()`/`pop()` are safe from any number of threads, at most `max_hazard_pointers` at a time.
- **Accepted for this build:** the book notes that comparing a pointer to an already-deleted node is technically undefined behaviour with the default `new`/`delete`. Don't write a custom allocator.

Tests to write:
1. **Every value comes out exactly once under contention.** As in `lock_free_stack` Test 1: 4 pushers each push 20,000 distinct ints, while 4 poppers pop until 80,000 successful pops in total. Assert every value was received exactly once. *Catches:* lost or duplicated nodes, and a node deleted while another thread can still dereference it (ASan reports use-after-free).
2. **A protected object isn't deleted until its hazard pointer is cleared, and then it's deleted with the right type.** Use the utility directly, with a small type whose destructor increments a global counter.
   - Create one object with `new`.
   - Thread A sets its hazard pointer to that object, signals the main thread, and waits.
   - The main thread calls `reclaim_later(obj)` and then `delete_nodes_with_no_hazards()`. Assert the counter is still 0.
   - Signal A to clear its hazard pointer, wait until it has, and call `delete_nodes_with_no_hazards()` again. Assert the counter is exactly 1.

   *Catches:* deleting without checking other threads' hazard pointers, deleting through `void*` (the destructor never runs), and never deleting.
3. **Threads that exit give their slot back.** Run 300 short-lived threads one after another (3 × `max_hazard_pointers`). Each calls `get_hazard_pointer_for_current_thread()`, stores the address of one shared global object into it, and exits. Assert none of them threw. After the last has been joined, assert `outstanding_hazard_pointers_for(&that_object)` is false. *Catches:* a slot not released at thread exit (the 101st thread throws "No hazard pointers available"), and a pointer left set after its thread is gone.

Self-explanation (2–4 sentences, closed-book, before review): Why does `pop()` store the hazard pointer and then re-read `head` in a loop? What can happen in the gap if it stores once and goes straight on to dereference? Why is it enough to check that `head` hasn't changed? And what does a thread's hazard pointer protect *after* a failed compare/exchange reloads `old_head`, until the loop sets it again?

Answer: if we take a look at the operations happening in the first half of pop(), we are taking a pointer to the old_head, the we are storing it in the hazard pointer for that thread, then finally we are using the statement `while (old_head && !head.compare_exchange_strong(old_head, old_head->next));` to remove the old_head and make the new one. we know this last statement ensures that two threads never remove the same node twice, the call `std::atomic<void*>& hp = hazard_ptr::get_hazard_pointer_for_current_thread();` ensures that each caller thread has its own hazard pointer, however the race window in the aforementioned windows means that there is a chance that two or more callers store the same one old_head node thats going to be removed by only one thread and to avoid this, we loop to ensure that the node stored in the hp actually matches the one that thread actually removed from the linked list.


### Beyond the book (optional, ungraded)

None this time.

---

## Review: 7.2.3 (2026-10-23)

All three warm-ups are blank. Confidence tags are on Q1–Q3 (all med). The self-explanation is written.

### Warm-up

- `4.1-notify-outside-lock` -> **miss** (blank). It had a streak of 2.
  - Answer: not a lost wakeup. `wait(lk, pred)` checks `!fills.empty()` while holding `m`: before it ever sleeps, and again every time it wakes. The push happens under `m`. Either the risk thread checks after the push (sees data, doesn't sleep), or it's already waiting, which atomically released `m`, so the later `notify_one()` reaches it.
  - The gain of notifying after unlocking: the woken thread doesn't immediately block again on the mutex the notifier still holds.
- `5.1-data-race-definition` -> **miss** (blank). It had a streak of 1.
  - Answer: a plain `void*` written by the owner and read by scanners is a data race, because the accesses aren't both atomic. In the book's version both are atomic, so it isn't one, even with no lock and no ordering.
  - F15's discarded value doesn't matter: the race is the two accesses themselves, not what's done with the result.
- `5.2-last-one-out-counter` -> **miss** (blank).
  - Answer: it can run **twice**. Two connectors do their `fetch_add`s, reaching 5 and then 6, before either of them loads, so both load 6. It can't run zero times: there are exactly 6 increments, and whoever does the sixth then loads 6 (or more, never less).
  - Fix: `if (connected.fetch_add(1) + 1 == 6)`. Each read-modify-write returns a distinct value, and only one of them returns 5.

### Quiz

1. **Miss** ((a) and (c)).
   - (a) > "the loop is doing what the book says we should do when the node ... turns out to have been deleted, we re-read it and re-publish it"

     That describes the loop, not the caveat that was asked for. The book says that using a pointer's value after its object is deleted is **technically undefined behaviour with the default `new`/`delete`**. You need an implementation that permits it, or a custom allocator.
   - (b) > "We choose _strong when the work being done in the retry loop is significant"

     Correct, and it matches the book: a spurious failure of `_weak` would reset the hazard pointer unnecessarily.
   - (c) Not answered. Your "(c)" answers (d). The answer to (c): clear it straight after the successful CAS. The node is now off the stack and only this thread can reach it.
   - (d), labelled (c) in your answer: still-hazardous nodes go back on the reclaim list and are left for a later `pop()`. Correct.
2. **Miss** ((b)).
   - (a) > "both end up running delete on the same current node ... UB"

     Correct: a double delete.
   - (b) > "i dont see any problem happening to another thread walking the list"

     The problem is in the line the comment points to: `delete current` while it's **still linked**. The shared head, or the previous entry, still points at freed memory. A concurrent `add_to_reclaim_list()` then sets its new entry's `next` to that freed entry, so the next walk reads freed memory. This happens even with one reclaiming thread, on its next call.
   - (c) > "all of its nodes are only now visible to the thread that has completed the exchange ... Other threads may push new nodes"

     Correct.
3. **Miss** ((b) and (c)).
   - (a) Correct: about 100 checks for its own node plus 100 per pending node, and the book's "often 100 times slower" figure.
   - (b) > "the excess entries are guaranteed to be unprotected"

     That's the right idea: at most `max_hazard_pointers` nodes can be protected at once. But two parts are missing or off:
     - **Why `max + 1` barely helps:** once the list is that long, you attempt to reclaim on almost every `pop()` and free only about one node each time.
     - **The amortized cost:** "about max_hazard_pointers atomic calls" isn't the book's figure. The book: at `2 * max` you reclaim at least `max` nodes, then go at least `max` pops before the next attempt, which is **about two nodes checked per `pop()`, one of them reclaimed**.
   - (c) > "the downside ... is the increased use of memory"

     Memory is only part of it. The book's downsides are that you now need an **atomic count** of the list, and threads still **contend for the list**. Per-thread lists remove both, at the cost of up to `max * max` allocated nodes. Your handling of an exiting thread's leftovers is right.

### Build

```
hazard_pointer_stack: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link FAILED: ODR multiple definition of: hazard_ptr::add_to_reclaim_list(...), hazard_ptr::delete_nodes_with_no_hazards(), hazard_ptr::get_hazard_pointer_for_current_thread(), hazard_ptr::outstanding_hazard_pointers_for(void*)
```

**Library.** `pop()` is correct: the inner loop sits inside the outer one, the hazard pointer is cleared after the CAS, and nodes are checked before deletion. The type-erased deleter and the reclaim-list `exchange` are right too. Putting everything in `namespace hazard_ptr` is a nice touch.
- **Scratch:** a version of `pop()` that sets the hazard pointer once without re-checking `head` gets heap-use-after-free from your Test 1 under ASan in 3 of 6 runs. Your `pop()` gave 0 of 3.

Three contract misses:
- **(a) The four non-template functions in `hazard_pointers.hpp` aren't `inline`,** so the two-TU link fails. This is the contract's first bullet, and the F3/F5 bug again. The variables are correctly `inline`.
- **(b) `~hp_owner()` resets the owner id before clearing the pointer.** The book does pointer first, then id.
  - In your order, once the id is reset another thread can claim the slot and set its hazard pointer. Your destructor's late `pointer.store(nullptr)` then wipes it out, leaving that thread's node unprotected.
  - Scratch: X's destructor had a 20 ms sleep added between the two stores to widen the gap, and Y claimed the slot in that gap. Y's hazard pointer was cleared in 3/3 runs with your order, and stayed set in 3/3 with the book's order.
  - Your Test 3 can't see this, because its threads run one at a time.
- **(c) The stack's destructor doesn't delete the nodes still on the stack.** It only calls `delete_nodes_with_no_hazards()`. Scratch: push 10, destroy, LSan reports **480 bytes leaked in 20 allocations**. Your Test 1 empties the stack, so the driver never shows it.

**Driver, against the Tests-to-write list.**
- Test 1: written, and it can fail (see the scratch result above). ✔
- Test 2: written, and both asserts can fail. The join ordering and its comment are right. ✔
- Test 3: written. The "no slot leak" check can fail. ✔ One note: an exception thrown inside a `std::thread`'s function isn't caught by your `try` in `main`; it calls `std::terminate`. So `assert(!threw)` can never fire, though a slot leak would still abort the program. A flag set inside the thread would make the assert real.

Self-explanation: written, but the model is off.
> "there is a chance that two or more callers store the same one old_head node thats going to be removed by only one thread and to avoid this, we loop"

Several threads holding hazard pointers to the same node is fine; that's normal. The danger is the gap **between reading `head` and publishing the hazard pointer**. In that gap no other thread knows you're about to use the node. Another thread can pop it, find no hazard pointers on it, and delete it, and your dereference is then use-after-free.
- Re-reading `head` after the store and finding the same value means the node was still on the stack *after* your hazard pointer became visible, so whoever removes it later will see it.
- A node can only be deleted after it's been taken off `head`, so "head unchanged" is enough.
- The last part of the prompt wasn't answered. After a failed CAS reloads `old_head`, the hazard pointer still names the **old** node. The new one is unprotected until the inner loop sets it again, which is why nothing is dereferenced before that.

Nits (no fix item):
- `compare_exchange_strong` in `push()` and `add_to_reclaim_list()`, which are plain retry loops where the book uses weak (warm-up item from 7.2.2).
- Debug `cout`s, and unused `<future>`, `<random>` and `<mutex>` in the driver.

### Queue updates

```
python3 tools/review.py log 4.1-notify-outside-lock miss --note "7.2.3 warm-up: left blank"
python3 tools/review.py log 5.1-data-race-definition miss --note "7.2.3 warm-up: left blank"
python3 tools/review.py log 5.2-last-one-out-counter miss --note "7.2.3 warm-up: left blank"
python3 tools/review.py add 7.2 7.2-hazard-pop-details "..." --kind miss --source "examples-of-lock-free-data-structures-notes.md 7.2.3 Q1(a)(c)"
python3 tools/review.py add 7.2 7.2-reclaim-list-exchange "..." --kind miss --source "examples-of-lock-free-data-structures-notes.md 7.2.3 Q2(b)"
python3 tools/review.py add 7.2 7.2-hazard-reclaim-cost "..." --kind miss --source "examples-of-lock-free-data-structures-notes.md 7.2.3 Q3(b)(c)"
python3 tools/review.py add 7.2 7.2-hazard-set-recheck-loop "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.3 (Listing 7.6 text; self-explanation)"
python3 tools/review.py add 7.2 7.2-hp-owner-slot-lifecycle "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.3 (Listing 7.7 text)"
python3 tools/review.py add 7.2 7.2-type-erased-deleter "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.3 (Listing 7.8 text)"
```

The concept texts are in `review-queue.csv`, via `review.py list -v --section 7.2`.

Results:
- The three warm-up items are back to streak 0 (due 10-24). Both misconception items lost their streaks, because the answers were blank.
- The three new miss items are due 10-23 and the three core items 10-24.
- 7.2 stays `current`; 7.2.4 onwards remain.

### Fix list

- **Opened F16** (`hazard_pointer_stack`):
  - (a) `inline` on the four non-template functions in `hazard_pointers.hpp`, so the two-TU link passes.
  - (b) `~hp_owner()`: clear the pointer, then reset the id.
  - (c) The stack's destructor must delete the nodes still on the stack.

  Re-verify with `verify.sh hazard_pointer_stack` and one `detect_leaks=1` run on a stack destroyed while non-empty.

### Beyond the book (ungraded)

None.

---

## 7.2.4 — Detecting nodes in use with reference counting (Listings 7.9–7.11)

```
$ python3 tools/review.py due   (2026-10-25; run on a copy, since the computer's shell was unavailable)
Due now: 104 active (104 overdue) | maintenance due: 1 | due in next 3 days: 0 | total active: 104, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.2-cas-weak-vs-strong  (sec 5.2, 2d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.3 Q2
  2. [misconception] 5.3-sync-needs-value-read  (sec 5.3, 2d overdue, streak 0)
     source: synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(b)(c)
  3. [misconception] 4.1-notify-outside-lock  (sec 4.1, 1d overdue, streak 0)
     source: waiting-for-an-event-or-condition-notes.md 4.1 Q7 (Correction 2026-10-05; Q7 was mis-graded at the original review)
Also due (next worksheets): 101 more (5.1-data-race-definition, 5.2-rmw-result-not-reload, 5.1-modification-order, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 16 (F1-F16)
Lint: 86 items more than 7 days overdue (review-only paused through ch7, per the 2026-10-16 plan)
```

Triage:
- **Listing 7.9 (`std::shared_ptr` as the node link):** why its atomic operations aren't guaranteed lock-free, and the check that tells you. Also why `counted_node_ptr` can go in `std::atomic<>`, and when that's lock-free. **Recall** (Q1).
- **The split-count arithmetic in `pop()`:** who deletes the node, and why `external_count - 2`. **Code-tracing** (Q2).
- **Listings 7.10–7.11 are a reusable mechanism, and 7.2.5 relaxes their memory orders, so they're a build:** `ref_counted_stack` (below). "Increase the count before dereferencing" and the whole-struct compare/exchange are covered by the build and its self-explanation, so they aren't quizzed.
- **Toolchain:** GCC implements a 16-byte `std::atomic` as calls into libatomic.
  - The new target's `CMakeLists.txt` links `atomic`.
  - **`tools/verify.sh` now adds `-latomic` whenever the compiler accepts it.** Without it, the ASan build doesn't link. Tested against a scratch copy of Listings 7.10–7.11: TSAN 2/2 clean, ASan+UBSan 2/2 clean, 2-TU link OK. Nothing changes for the other tasks.

The warm-ups are short this time. **One low/med/high tag per answer**, warm-ups included.

### Warm-up

1. Listing 7.10's `push()` uses `compare_exchange_weak`; Listing 7.6's `pop()` uses `compare_exchange_strong`.
   (a) What is a spurious failure, and why can it happen on a machine with no single compare-and-exchange instruction?
   (b) On such a machine, what does `compare_exchange_strong` contain internally? So what is a `_strong` call inside your own retry loop?
   (c) Give chapter 5's rule for when `_strong` is still the better choice in a loop, and the specific reason the book gave for Listing 7.6.

Answer:


2. The feed thread publishes a snapshot; the strategy thread reads it.

```cpp
// feed thread
snap.bid = 100;  
snap.ask = 101;              // plain ints
seq.store(1, std::memory_order_release);

// strategy thread
int s = seq.load(std::memory_order_acquire);
int b = snap.bid;                              // (R1): after the load, but outside the if
if (s == 1) {
    int a = snap.ask;                          // (R2)
}
```

   Is (R1) ordered after the feed thread's writes **when `s == 1`**? Is it **when `s == 0`**? Is (R2)? Name the relation each time. Where it's missing, say exactly what the program has. Is "(R1) gets either the old bid or 100" a correct description?

Answer: The way i understand is that the strategy thread is missing a while loop over the load command because these are two different threads, nothing is stopping hte strategy thread from proceeding forth even if the 1 hasnt yet been stored in seq in the feed thread code, and as we already know, the synchronizes with relation between the store and load on seq only happen when the store has happened on the feed thread.

now assuming the store has happened on seq in the feed thread, in that case, yes, all statements following the load will be able to see the changes made in the feed thread above the store there. so if thats the case, then the answers to the questions are - yes R1 is ordered after the feed thread's writes , i.e., when `s == 1`. No it is not ordered after feed thread's writes when `s == 0`. between the threads, the relation is synchronizes with and within the threads themselves we have the sequenced before relationships. If the happens before relation fails to establish then we have race condition I think because there is no ordering or protection between the reads on snap.bid and snap.ask in the feed thread and in the strategy thread which also answers the last part of the question, i.e., the statement "(R1) gets either the old bid or 100" isn't correct as we'd have a race condition if the happens before isn't established.


3. The gateway thread sets `acked = true` while holding `m`, releases `m`, then calls `cv.notify_all()`. The strategy thread does `std::unique_lock lk(m); cv.wait(lk, [&]{ return acked; });`. A teammate says the notify must move inside the lock, or the strategy thread can miss it. Right or wrong? Answer in terms of when `wait()` checks `acked`, and under which lock. Then say what the book says notifying after unlocking gains.

Answer:


### Quiz

1. (Recall) Listings 7.9 and 7.10.
   (a) Some operations on `std::shared_ptr<>` are atomic. Why does the book say they aren't guaranteed lock-free, and which call tells you whether they are on your platform? If it returns true, what does Listing 7.9 give you?
   (b) Why can `counted_node_ptr` be used with `std::atomic<>` at all? On which platforms does the book expect `std::atomic<counted_node_ptr>` to be lock-free, and what happens inside `std::atomic<>` where it isn't?
   (c) `push()` creates the node with `internal_count = 0` and `external_count = 1`. What does that 1 count?

Answer (med):

(a) for the current implementation of a lock free stack where the entire idea is to have a system where we make push and pop atomic which is straightforward with the CAS operations, and use reference counting to then solve the deleted node's memory reclamation problem for which we have seen several approaches including maintaining a linked list and deleting it when no other threads are accessing the pop() function or using hazard pointers to track if one or more threads hold references to nodes we are about to reclaim. The current approach relies on counting references or pointers to the node we are about to reclaim and this is exactly what shared_ptr<> already does internally which makes it a great candidate to make such a stack. problem is, if the internal implementation of atomic operations for shared_ptr are not lock free, then calling a stack implementation lock-free wouldnt make much sense or have the same benefits. the standard says that depending on what architecture and system you have, the shared_ptr's atomic operations might or might not be atomic. To find out at runtime, the code is:

```cpp
std::shared_ptr<node> p;
std::atomic_is_lock_free(&p);
```

(b) the counted_node_ptr struct has the following definition:

```cpp
struct counted_node_ptr {
   int external_count;
   node* ptr;
}
```
now this means that on a 64 bit system, ptr will be 8 bytes or 64 bits and int will be 4 bytes or 32 bits. combined, both exceed 64 bits, however, if DWCAS - Double Word Compare And Swap - was enabled on the system, then that means lock free atomic operations for counted_node_ptr, more precisely atomic CAS operations on two words or 128 bits / 16 bytes, simultaneously - meaning that the counted_node_ptr will qualify and our implementation of stack will be lock free. This is expected to work on 64 bit systems and if its unavailable then std::atomic<counted_node_ptr> and likely most std::atomic<> operations wouldnt be lock free, in which case, the shared_ptr implementation of the reference counting based stack is better since its easier to reason about.

(c) to put it simply, the external_count, counts the number of active reference holding threads to the node while the internal_count, counts the number or threads that are done holding the reference to the node.

- external_count => how many threads currently hold the pointer and might dereference it.
- internal_count => how many threads are finished with this pointer and wont use it again.

2. (Code-tracing) The stack holds only node X: `head = {external_count = 1, ptr = X}`, and `X->internal_count = 0`. Threads A and B both call `pop()`.
   1. A: `increase_head_count` succeeds, so `head = {2, X}`, and A holds `{2, X}`.
   2. B: `increase_head_count` succeeds, so `head = {3, X}`, and B holds `{3, X}`.
   3. B: `head.compare_exchange_strong(old_head, ptr->next)` succeeds, so B owns X.

   Two possible orders for what remains:
   - **Order P:** B does its `fetch_add`, then A's compare/exchange fails and A does its `fetch_sub(1)`.
   - **Order Q:** A's compare/exchange fails and A does its `fetch_sub(1)`, then B does its `fetch_add`.

   (a) For each order, give `count_increase`, every value `fetch_add`/`fetch_sub` returns, the final `internal_count`, and which thread deletes X.
   (b) Why is `count_increase` the external count **minus 2**? Say what each of the 2 stands for.
   (c) After A's compare/exchange fails in step 4, what does A's `old_head` hold, and what does A do next?

Answer (med):

(a) For Order P, where B does the fetch_add and A does fetch_sub - the external count is 3, B takes ownership and gets count_increase of 1 after it subtracts 2 (one of the head and the other being its own reference to the former head). the internal_count is 0, it gets that from the fetch_add and puts in it the 1. comparing 0 and -1 which is the if condition of deletion, we know B will not delete the node. A on the other hand will run the else part since its CAS fails and its not allowed ownership of the head in its stack's old_head, so in the else block, A will reduce the internal count by 1 and get the old value which is 1 and comparing it to 1, it means that A was the last reference holding thread and so it will delete the node X. 
In Order Q, where A does the fetch_sub and B does the fetch_add, the opposite will happen, i.e., B will be the one to delete node X.

(b) the internal count is kept up with the number of external references held to the node, this is done in this line of the CAS success if block:

`if(ptr->internal_count.fetch_add(count_increase) == -count_increase){ delete ptr;}`

this means that internal_count isnt running a negative count of how many threads were former holders of the node, that could run into large negatives, rather, it too is storing the number of active referencing threads to the node, as we see in above line. the minus 2 on the external before making it internal is because as we wrap up this branch of pop() with successful CAS, we are also potentially deleting the former head and relinquishing the reference to it from this caller, thats two reference to the node, the head reference and this thread's reference - hence the -2 before storing reference count into the internal_count.

(c) After A's compare/exchange fails in step 4, A's `old_head` holds the recent copy of the head counted_node_ptr whose exclusive ownership it could not secure. After this failed CAS it will move to the else block where it will decrement the internal_count of the node its old_head points to and then attempt to delete it if its the last reference, otherwise it will try the loop again and try the same CAS again but will get an updated old_head in the process.

### Build task — `ref_counted_stack` (Listings 7.10–7.11)

Folder: `ch07_designing_lock_free_concurrent_data_structures/ref_counted_stack/` (scaffolded: `CMakeLists.txt`, which links `atomic`, the root `add_subdirectory` line, and empty header and driver). Book closed.

Interface:

```cpp
template<typename T>
class ref_counted_stack {
public:
    ref_counted_stack();
    ~ref_counted_stack();
    ref_counted_stack(const ref_counted_stack&) = delete;
    ref_counted_stack& operator=(const ref_counted_stack&) = delete;

    void push(T const& data);
    std::shared_ptr<T> pop();     // nullptr when the stack is empty
};
```

Thread-safety contract:
- **Layout, as in the book:**
  - `struct counted_node_ptr { int external_count; node* ptr; };`
  - a `node` holds `std::shared_ptr<T> data`, `std::atomic<int> internal_count` and a plain `counted_node_ptr next`;
  - `head` is a `std::atomic<counted_node_ptr>`.

  No mutex, no hazard pointers, no pending list.
- `push()`: the node is fully built (data allocated, `internal_count = 0`, `external_count = 1`, `next` set) before a compare/exchange loop publishes it. `next` is never written after that.
- **`pop()`:**
  - Before dereferencing a node pointer read from `head`, the thread increases that node's external count in `head` with a compare/exchange on the **whole** `counted_node_ptr`.
  - It removes the node with a compare/exchange on `head`.
  - The winner moves the data out, then adds `external_count - 2` to `internal_count`. It deletes the node if the count reached zero.
  - A thread whose removal attempt fails subtracts 1 from `internal_count`, and deletes the node if that made it zero, before retrying with the fresh `head`.
  - **Every node is deleted exactly once, and only after no thread can still dereference it.**
- The destructor (never concurrent) leaves no node and no `T` allocated. The book's `while(pop());` is fine.
- Default `memory_order_seq_cst` throughout (7.2.5 relaxes it). `push()`/`pop()` are safe from any number of threads.
- On GCC, `head.is_lock_free()` is false: libatomic uses a lock for 16-byte atomics. That's expected, not a bug to fix.

Tests to write:
1. **Every value comes out exactly once under contention.** 4 pushers each push 20,000 distinct ints, while 4 poppers pop until 80,000 successful pops in total. Assert every value was received exactly once. *Catches:* lost or duplicated nodes, a node freed while still in use (ASan use-after-free), or freed twice (ASan double-free).
2. **Exactly one winner per item.** For 500 rounds: push one value (the round number), release 4 poppers together, each calling `pop()` once. Assert exactly one non-null result per round, equal to the round number, and three `nullptr`s.

   Only start round r+1 after all 4 poppers have finished round r: count them in with an atomic counter, and have the pusher wait for 4. That keeps the rounds separate, **and with that wait it can't hang** (F15's Test 2 hung).

   *Catches:* two threads returning the same node.
3. **Destroying a non-empty stack frees everything.** Use a type that counts live instances: the constructor and copy constructor increment a global counter, and the destructor decrements it. Push 10 values, then assert the counter is 10: only the stack's copies are alive. Destroy the stack without popping, and assert the counter is 0. *Catches:* a destructor that leaks nodes or their data (F16's leak).

Self-explanation (2–4 sentences, closed-book, before review): Why must `pop()` increase the external count *before* it dereferences `ptr`? What can another thread do in between if it didn't? Why does `increase_head_count()` use a compare/exchange on the whole `counted_node_ptr`, rather than incrementing a count held somewhere else?

Answer:


### Beyond the book (optional, ungraded)

None this time.

---

## Review: 7.2.4 (2026-10-25)

All three warm-ups and the self-explanation are blank. Q1 and Q2 have confidence tags (med).

### Warm-up

- `5.2-cas-weak-vs-strong` -> **miss** (blank, second worksheet running).
  - (a) `_weak` may fail even when the value matched, because the machine couldn't finish the sequence atomically (for example, the thread was switched out).
  - (b) There, `_strong` contains its own loop, so `_strong` in your retry loop is a loop inside a loop.
  - (c) Prefer `_strong` when the value to store is expensive to compute. For Listing 7.6: a spurious failure would reset the hazard pointer unnecessarily.
- `5.3-sync-needs-value-read` -> **miss** (blank).
  - When `s == 1`, (R1) **is** ordered: it's sequenced after an acquire load that read the release store, so it doesn't need to be inside the `if`.
  - When `s == 0`, there is no happens-before. That's a **data race, so undefined behaviour**, not "the old bid".
  - (R2) runs only when `s == 1`, so it's ordered.
- `4.1-notify-outside-lock` -> **miss** (blank, second worksheet running).
  - The teammate is wrong. `wait()` checks `acked` while holding `m`: before it ever sleeps, and on every wake-up. The gateway set `acked` under `m`, so either the strategy thread sees it on its first check, or it's already waiting and gets the notify.
  - The gain: the woken thread doesn't immediately block on a mutex the notifier still holds.

### Quiz

1. **Miss.**
   - (a) > "the standard says that depending on what architecture and system you have, the shared_ptr's atomic operations might or might not be atomic"

     The call is right (`std::atomic_is_lock_free(&p)`). Two things are missing:
     - **The book's reason:** `std::shared_ptr` is used in many contexts, and making its atomic operations lock-free would add overhead to **every** use of the class.
     - **What a `true` gives you:** the whole reclamation problem goes away; just use Listing 7.9.

     ("Might or might not be *atomic*" should read *lock-free*: they're atomic either way.)
   - (b) Double-word compare-and-swap is the right platform condition. But the book's point is that otherwise **`std::atomic<>` uses a mutex inside**, so the "lock-free" algorithm is lock-based after all. That's why it suggests Listing 7.9. It isn't about the shared_ptr version being "easier to reason about".

     "Likely most `std::atomic<>` operations wouldn't be lock free" is wrong. Only types too big for the platform's atomic instructions fall back to a lock.
   - (c) > "external_count => how many threads currently hold the pointer"

     The question was what *that* 1 counts. Book: a new node has exactly one external reference, **the `head` pointer itself**.

     Also, `internal_count` isn't "threads that are done". Finished readers *decrement* it, the winner adds the external count back, and the node is freed when the two sum to zero.
2. **Hit**, with corrections.
   - (a) Order P is traced correctly: `count_increase` = 1, B's `fetch_add` returns 0 (not −1, so no delete), A's `fetch_sub` returns 1, so A deletes and the final count is 0.

     For Order Q you gave only the conclusion. The values: A's `fetch_sub` returns 0 (count −1, no delete). Then B's `fetch_add(1)` returns −1, which equals −1, so B deletes. Final count 0.
   - (b) > "the head reference and this thread's reference"

     Correct.
   - (c) > "it will decrement the internal_count of the node its old_head points to"

     Wrong node. After the failed exchange, `old_head` holds the **new** head; here that's X's `next`, a null pointer. The decrement goes to **X**, through the `ptr` saved before the exchange; your code gets this right (`ptr->internal_count`). Then A doesn't "try the same CAS again". It loops to `increase_head_count` on the new head, finds `ptr == nullptr`, and returns an empty `shared_ptr`.

### Build

```
ref_counted_stack: TSAN 3/4 clean (0 race reports) | ASan+UBSan 0/4 clean | 2-TU link OK
```

Run with `-n 4 -t 10`. Every failure is **Test 2 hanging**: the run times out after "TEST 1 PASSED". None is a sanitizer error. ASan is slower, so it hangs more often. Natively the full driver hung in 8 of 20 runs.

**Library: correct.** It's a faithful Listings 7.10–7.11. The external count is increased before `ptr` is dereferenced, the count goes to `ptr`, and the destructor pops everything.

Scratch check, driver with Test 2 removed:
- TSAN 5/5 clean, 0 reports;
- ASan+UBSan+LSan 5/5 clean, 0 leaks;
- Test 3 passes.

Note: `push()` uses `compare_exchange_strong` where the book uses weak (warm-up 1's topic).

**Driver, against the Tests-to-write list.**
- Test 1: written, and its asserts can fail. ✔
- **Test 2 is F15's Test 2 copied over: not the listed test, and it hangs.** The spec described the round-by-round version (all 4 poppers pop once per round; the pusher waits until all 4 have finished before pushing the next) because F15's version hangs. This one has the same hand-off flag and the same hang, and its assert is overall conservation, not "exactly one winner per round".
- Test 3: written. Both asserts can fail; a destructor that skips the nodes leaves the counter at 10. ✔
- **Self-explanation: blank.**

`tools/verify.sh`: the `-latomic` change works, and this target links. My copy over on Sunday had dropped the script's executable bit; I've restored it, so git now shows only the content change.

### Queue updates

```
python3 tools/review.py log 5.2-cas-weak-vs-strong miss --note "7.2.4 warm-up: left blank (2nd time running)"
python3 tools/review.py log 5.3-sync-needs-value-read miss --note "7.2.4 warm-up: left blank"
python3 tools/review.py log 4.1-notify-outside-lock miss --note "7.2.4 warm-up: left blank (2nd time running)"
python3 tools/review.py add 7.2 7.2-shared-ptr-and-dwcas "..." --kind miss --source "examples-of-lock-free-data-structures-notes.md 7.2.4 Q1"
python3 tools/review.py add 7.2 7.2-split-count-arithmetic "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.4 Q2"
python3 tools/review.py add 7.2 7.2-increment-before-deref "..." --kind core --source "examples-of-lock-free-data-structures-notes.md 7.2.4 (Listing 7.11 text; self-explanation)"
```

Results:
- The three warm-up items are at streak 0 (due 10-27).
- The new miss item is due 10-26 and the two core items 10-27.
- 7.2 stays `current`; 7.2.5 (relaxed orders on this stack) is next.

### Fix list

- **Opened F17** (`ref_counted_stack`):
  - (a) Test 2: write the listed round-by-round test (4 poppers once per round; the pusher waits for all 4; exactly one non-null equal to the round, three nulls). It must not hang.
  - (b) The self-explanation.

  Library verified in scratch (Tests 1 and 3). Re-verify: `verify.sh ref_counted_stack` (default 20).

### Beyond the book (ungraded)

None.
