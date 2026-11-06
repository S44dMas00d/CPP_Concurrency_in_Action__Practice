# 6.2 — Lock-based concurrent data structures

## 6.2 intro, 6.2.1 — A thread-safe stack using locks (Listing 6.1)

```
$ python3 tools/review.py due   (2026-10-07)
Due now: 71 active (71 overdue) | maintenance due: 1 | due in next 3 days: 4 | total active: 77, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 4.1-notify-outside-lock  (sec 4.1, 1d overdue, streak 1)
     source: waiting-for-an-event-or-condition-notes.md 4.1 Q7 (Correction 2026-10-05)
  2. [miss] 5.2-cas-update-loop  (sec 5.2, 11d overdue, streak 0)
     source: atomic-operations-and-types-notes.md 5.2.3 build self-explanation
  3. [miss] 5.2-memory-order-by-operation  (sec 5.2, 11d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (5.2.1 Q6 'seq_rel'; 5.2.2 Q2 clear() disallowed orders)
Also due (next worksheets): 68 more (3.3-map-concurrent-write-unsafe, 4.2-packaged-task-conversion-rule, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 11 (F1-F11)
Lint: 27 items more than 7 days overdue (review-only paused during ch6, per the 2026-10-07 plan)
Git: clean
```

Triage:
- What's new compared with 3.2.3: Listing 6.1 is your 3.2.3 `threadsafe_stack` with one change. `pop()` **moves** out of `data.top()`; yours copies. Everything else 6.2.1 adds is analysis of that code.
- The exception walk-through: every place the stack can throw, and why each one is safe (the lock is taken first; `empty_stack` is thrown before anything is modified; `make_shared` or the move can throw, but the stack hasn't been touched yet; `data.pop()` and the return can't throw). This is **code-tracing** against variants that break it (Q1).
- Deadlock from calling user code while the lock is held (copy/move constructors, copy/move assignment, a user-defined `operator new`), and the book's verdict that avoiding it is the user's job: **code-tracing** (Q2).
- Why the `empty()`/`pop()` race isn't a problem here; which members aren't safe to call concurrently; why the copy constructor locks only `other.m`: **recall** (Q3).
- **No build.** Listing 6.1 already exists as your 3.2.3 build, and the only code difference is a one-line move in each `pop()`. The ledger's "extend the stack" plan has nothing new to extend here. 6.2.2's queue is where the real extension is. If you want one anyway, there's a small optional extension: switch `pop()` to move, and add a driver test with a type whose move throws on demand, checking that the element survives a failed pop. Say so and I'll spec it.

Add a low/med/high confidence tag to each quiz answer.

### Warm-up

1. A fills consumer runs `std::unique_lock<std::mutex> lk(m); cv.wait(lk, [&]{ return !fills.empty(); });`. The producer runs `{ std::lock_guard<std::mutex> g(m); fills.push(f); } cv.notify_one();`. A colleague worries about one timing: the consumer's predicate check returns `false`, and *before the consumer actually goes to sleep*, the producer pushes and calls `notify_one()`, so the notify is lost and the consumer sleeps with a fill waiting. Can that interleaving happen? Explain using what `wait(lk, pred)` holds at each step and what the producer needs before it can push.

Answer: this interleaving cant happen because the producer needs the mutex lock for mutex m shared between producer and consumer to push and notify, and until the predicate has run and returned false, the mutex lock wont be given up by the wait(). thats why we know mutexes are also vehicles for synchronizing the runs of methods in a data structure and mutexes shared among methods help synchronize their usage. 

To explain the working of wait, here is a detailed breakdown of its steps:
- acquires mutex
- checks predicate:
   - if predicate returns true then returns with the mutex locked so code below can run
   - if predicate returns false then it does the steps below:
- unlocks the mutex
- puts the thread to sleep.
- waits for notify to be called.

so as we can see, there isnt any opening for the publish to happen and notify to come when wait() thread is awake and holding a mutex.

2. A trading system records the worst latency seen, in `std::atomic<std::int64_t> worst_us{0}`. Version A:

```cpp
void report(std::int64_t v) {
    if (v > worst_us.load()) worst_us.store(v);
}
```

   Threads T1 and T2 call `report(900)` and `report(1200)` at the same time, starting from `worst_us == 500`. (a) Give an interleaving where the final value is 900. (b) Version B replaces the body with a `compare_exchange` loop. Say what the **loop** contributes and what the **compare-exchange** itself contributes, and why B can't end at 900. (c) Why does `compare_exchange_weak` work unchanged in B?

Answer (med):

(a) the interleaving i can forsee where final value is 900 is as follows - both `report(900)` and `report(1200)` run together, both race to if check and one after the other both pass since `worst_us` stores 500 for the checks of both, i.e. no store ran yet. then both race between load and store till store(1200) wins and executes first but then store(900) runs and so in this interleaving, store(900) comes out as the final value.

(b) so the text for this part is a bit confusing because when its said that we replace the body with a `compare_exchange` loop, then what exactly does it mean because we have two scenarios that come to mind, here is one:

```cpp
void report(std::int64_t v) {
   std::int64_t expected = worst_us.load();
   while(!worst_us.compare_exchange_strong(expected, v)){}
}
```
for this case i dont think the statement that `worst_us` cant end in 900 holds up because there isnt a greater than comparison in the while loop, there is just a equal to comparison in the CAS operation and so if load gives 500 and `report(900)` looses to `report(1200)` to that while loop's CAS, i.e., then first we get 1200 in there on first try since expected was right and then the `report(900)` gets to the CAS and there it fails the first comparison but expected gets updated and it wins the second so it too gets stored so now we have 900 in `worst_us` and so the loop actually contributed what it shouldnt have and thats another try to the smaller value, not that it matters since a lone CAS without any gate would also give this above 900 final value if it was first to the CAS operation.

then the second snippet that comes to mind that might work is this one:

```cpp
void report(std::int64_t v) {
   std::int64_t expected = worst_us.load();
   if (v > expected){
      while(!worst_us.compare_exchange_strong(expected, v)){
         if (expected > v) break;
      }
   }
}
```
here if we suppose that `report(1200)` races to and wins over `report(900)` at the if statement, then it goes into the if statement body and writes to worst_us the value 1200, now if `report(900)` wins the if statement and enters the in the worst case, if the expected load didn't sync with the write by `report(1200)` then we get CAS fail first time since expected will be 500 but the actual value will be 1200, so expected will be reset to 1200 and the loop body will execute. in the loop body I have the check where if the supplied value is less than the updated expected then the loop shouldn't update and break and thats whats gonna ensure that we don't end up with 900 instead of 1200 in worst_us as that would be logically the wrong number to end up with in there.

(c) well if we used `compare_exchange_weak` instead of the strong variant we might end up entering the loop body with a non updated expected value which means that if the exchange was going to run, i.e., the last expected was smaller than v, then the CAS is going to get another chance anyways since it has already passed the first if statement.

3. For each call, list the memory orders it may take: (a) `flag.clear(order)` on a `std::atomic_flag`, (b) `px.load(order)`, (c) `px.exchange(v, order)`. Then name the three orders a store such as `clear()` is **not** allowed to take, and give the one-line reason they make no sense for a store.

Answer: 

(a) `flag.clear(order)` - clear is a write operation that sets the value of std::atomic_flag to false. it may take the memory orders: std::memory_order_release, std::memory_order_relaxed, std::memory_order_seq_cst.

(b) `px.load(order)` - load is a read operation and it may take the orders: std::memory_order_acquire, std::memory_order_consume, std::memory_order_relaxed, std::memory_order_seq_cst.

(c) `px.exchange(v, order)` - exchange is a read and write operation, also called read-modify-write or RMW operation and it can accept all six memory orders, acq_rel, seq_cst, release, acquire, consume, relaxed.

clear being a store or write operation cannot take memory orders meant for read semantics like acquire or acq_rel, this is because these orders are meant for creating the opportunity of other load operations on this atomic variable to sync with the write, a write syncing with a write however wouldn't make sense.


### Quiz

1. (Code-tracing) (a) Go through the first `pop()` overload in Listing 6.1 line by line. Name every operation that can throw, and say why the stack is still intact after each one. Also name the operations that can't throw.

   (b) A colleague "simplifies" both overloads:

```cpp
std::shared_ptr<T> pop() {                        // variant X
    std::lock_guard<std::mutex> lock(m);
    if (data.empty()) throw empty_stack();
    T v(std::move(data.top()));
    data.pop();
    return std::make_shared<T>(std::move(v));
}

void pop(T& value) {                              // variant Y
    std::lock_guard<std::mutex> lock(m);
    if (data.empty()) throw empty_stack();
    T tmp(std::move(data.top()));
    data.pop();
    value = std::move(tmp);
}
```

   For each variant, which throwing step now comes *after* the stack has been modified, and what happens to the popped item if it throws? State the ordering rule Listing 6.1 follows that these variants break.

Answer (med):

(a) here is the first pop() overload in listing 6.1:
```cpp

std::shared_ptr<T> pop()
{
   std::lock_guard<std::mutex> lock(m);
   if(data.empty()) throw empty_stack();
   std::shared_ptr<T> const res(
   std::make_shared<T>(std::move(data.top())));
   data.pop();
   return res;
}
```
lets go line by line
- locking the mutex can throw but the chances of that happening are extremely low and this could happen due to heavy resource constraints or issue with the mutex. Also lock_guard ensures that mutex gets unlocked when it goes out of scope from any path so there is no danger of mutex escaping the scope in locked form.
- the data empty check wont throw.
- the move might throw or the heap allocation of the moved internal data might throw due to resource constraints on a heavily loaded system. this throw however still wont leave any thing broken. the heap allocation has to happen before the move on data.top() and move itself doesnt move anything, it just converts the data.top() into an Rvalue so when the constructor run has to be decided the Rvalue based move constructor can be called. So with this order of events, we ought not to worry about invalidating or causing data loss of the stack data member.
- data.pop() shouldn't throw either.

(b) 

In variant X, we move construct the data.top() into a local variable v which might throw but thats no problem since we havent popped it off the stack yet so no data loss, however, once we cross the pop, we make a heap allocation for the shared pointer we are trying to return which we know can throw in the allocation or in the move construction, so if that happens, we lost the data as we have already popped it off the stack and the local variable holding the data was just that `local` so that gets destroyed.

In variant Y there is a similar problem to X but in this case the copy or move assignment operators can throw as we assingn the local tmp to value and if that throw happens, we have already popped the data.top and the local will get destroyed as we leave the scope and so we have a similar data loss issue.

2. (Code-tracing) A trading system keeps failed orders in `threadsafe_stack<order> retries;` (Listing 6.1). `order` logs every move:

```cpp
std::mutex audit_m;
std::vector<std::string> audit_log;

struct order {
    std::string id;
    explicit order(std::string i) : id(std::move(i)) {}
    order(order&& o) : id(std::move(o.id)) {
        std::lock_guard<std::mutex> lk(audit_m);
        audit_log.push_back("moved " + id);
    }
    order& operator=(order&& o) {
        id = std::move(o.id);
        std::lock_guard<std::mutex> lk(audit_m);
        audit_log.push_back("assigned " + id);
        return *this;
    }
};

// Thread A
{
    std::lock_guard<std::mutex> lk(audit_m);
    if (!retries.empty()) audit_log.push_back("retries pending");
}

// Thread B
std::shared_ptr<order> o = retries.pop();
```

   (a) Give the interleaving that deadlocks A and B. Name which mutex each thread holds and which it's waiting for.
   (b) A later change makes `order`'s move constructor also call `retries.empty()` "to log the queue depth". What happens now when a single thread calls `retries.pop()`, even with no other thread running?
   (c) List every point in Listing 6.1 where user code runs while `m` is held. Which `order` member runs at each point?
   (d) What does the book conclude about whose job it is to prevent this, and why can't the stack reasonably avoid it?

Answer (med): 

(a) thread A takes lock of mutex audit_m, but then right away, thread B runs retries.pop() and retries.pop has its own mutex for the stack which it takes and then it goes into the pop method which will call the move constructor of the order struct to then pop the retries.top() order off before putting it onto the shared pointer for its return, However, here comes the issue, the thread A already has the lock audit_m acquired, which is needed by the move constructor so that the thread B can proceed with the move and then pop. so thread B is waiting on thread A to let go of the audit_m mutex so that it can proceed with the  move construction and then the pop where as thread A is waiting for the retries mutex which is undoubtedly locked in the retries.pop() function, and so we have a deadlock interleaving.

(b) so order's move constructor is now somehow calling retries.empty and thus trying to lock retries mutex. This is a deadlock from a single thread since if we start the chain with retries.pop() we have now the retries mutex locked, this pop() method, we know from the listing of threadsafe_stack, calls the move or copy constructor of the object and in this case it will call the move constructor, then when we do call the move constructor and it again tries to lock the retries mutex then we have a deadlock since it wont move ahead and return.

(c) all methods except for the default constructor and destructor, including the copy constructor of the threadsafe_stack, use std::lock_guard as the first line of the function bodies. this means that virtually all major functions will run some order member function under lock.  `threadsafe_stack(const threadsafe_stack& other)` will run the assignment operator of order, i.e., `order& operator=(order&& o)` when we run the line `data=other.data;` in it. threadsafe_stack::push and both threadsafe_stack::pop() overloads will run the move constructor of order, i.e., `order(order&& o)` when they run the lines `data.push(std::move(new_value));`, `std::shared_ptr<T> const res(std::make_shared<T>(std::move(data.top())));` and `value=std::move(data.top());` respectively.

(d) the book's conclusion about this is that the burden of responsibility lies on the user, i.e., the user needs to ensure that they dont try to lock mutexes inside the methods which are going to be called by the threadsafe_stack such as the constructors, assignment operators and destructor. the stack cannot defend against possible deadlock scenarios because consider the one solution there is of using std::lock() to simultaneously lock mutexes to avoid deadlock, even if we were to somehow ascertain if the incoming object's methods have or dont have the use of mutex, to mould the logic of the stack's methods to conform to that of the object's method would render it ineffective or open to races, or otherwise unapplicable to any types other than the one its specialized for. Frankly, it just doesnt make sense.



3. (Recall) (a) There's a race between `empty()` and either `pop()`. Why does the book say it isn't problematic for this stack? What interface choice avoids the race that `std::stack`'s separate `top()` and `pop()` would have?
   (b) Which members are *not* safe to call concurrently with the others? Why does the book consider that acceptable, and what must the user guarantee?
   (c) The copy constructor locks `other.m` but never its own `m`. Why is that enough?

Answer (med): 

(a) yes there is a race window between empty() and pop() however this isnt a problem because inside the pop() method there is an empty check as well, `if(data.empty()) throw empty_stack()` so there wont be the interleaving where we have empty check on thread A which comes false, only for B to pop the element before A can, so even if another thread snatched the element in pop() the current thread will not  try to pop off an empty stack and result in UB but rather be caught in internal empty check and throw a particular error that can be caught in calling scope. 

The interface choice to avoid top() and pop() race was to just get rid of top() and inculcate its functionality in pop() such that calling pop just returns the top element, there are then the reference or shared pointer flavors which we have discussed a plenty.

(b) It is not safe to call the default constructor and destructor with the other methods and these require being run exclusively by one thread before and after all other methods are run respectively. this, like the other burden of responsibility of not passing in objects with mutex calls in methods relevant to the stack, is an acceptable burden of responsibility given to the user and is easy to ensure with the use of synchronization primitive like call_once and once_flag or an atomic flag. the user must guarantee that while the constructor and destructor run, no other method of that object must be run on any other thread concurrently.

(c) because its the copy constructor and not copy assignment, which means we have an empty data stack and also since the object is being constructed, no other thread will have the reference to it, and for these reasons the chances of data races are zero which makes it ok to not lock the mutex of `this` instance, however it is necessary to lock the mutex of `other` instance because its probably not empty and we need to ensure that while we are running its copy assignment operator of its data member, none of its own methods can write to the data which could lead to UB and corrupt data reads on the new object.


### Beyond the book (optional, ungraded)

Listing 6.1's `pop()` does `std::make_shared<T>(std::move(data.top()))`. The book says that if the move constructor throws, "you still haven't modified the underlying stack". Suppose `T`'s move constructor can throw *after* it has already moved some members out of the source. What state is the top element in after the exception? What standard-library helper do containers such as `std::vector` use during reallocation to avoid exactly this, and what does it choose between? Not queued unless you say "queue it".

Answer: 


---

## Review: 6.2 intro, 6.2.1 (2026-10-08)

Confidence tags: every quiz answer has one (all med), and so does warm-up 2. Thanks; that's what the framework asks for.

My slip: warm-up 2(b) said "a `compare_exchange` loop" without saying what the loop tests. You had to guess the design, and you rightly showed that a bare CAS-retry loop *can* end at 900. The probe should have read "a loop that retries while `v` is still larger than the current value". Graded against what you inferred.

### Warm-up

- **4.1-notify-outside-lock -> hit.** > "the producer needs the mutex lock … to push and notify, and until the predicate has run and returned false, the mutex lock wont be given up by the wait()". This is the probed point: the predicate is checked under `m`, and the push needs `m`. So the push lands either before the check (the predicate is true) or after the consumer is waiting (the notify wakes it). Nit: your step list shows "unlocks the mutex" and "puts the thread to sleep" as separate steps. Written that way, the colleague's gap would exist. `wait()` does the two together, which is what closes it.
- **5.2-cas-update-loop -> hit.**
  - (a) Right: > "both pass since `worst_us` stores 500 … store(1200) wins and executes first but then store(900) runs".
  - (b) Your second snippet is a correct fetch_max loop, and the trace is right. The CAS fails because the stored value is 1200, not the 500 you expected; `expected` is updated to 1200; your `if (expected > v) break;` then stops 900 from being written. Stated directly, the split the question asked for is:
    - **the compare-exchange** writes only if the value is still what you last saw, as one indivisible step, so no store can be based on a stale read;
    - **the loop** re-runs the `v > current` decision against the fresh value after every failure.
  - Nit: > "if the expected load didn't sync with the write by `report(1200)`". Synchronization isn't involved. Whatever the first load returned, the CAS is what catches the change.
  - (c) Right: a spurious failure just means one more trip round the loop, with the same check.
- **5.2-memory-order-by-operation -> miss.** The per-operation lists (a)–(c) are all right. The question then asked for the **three** orders a store can't take. > "cannot take memory orders meant for read semantics like acquire or acq_rel" names two; **`consume`** is the third. The reason is also the wrong way round: > "these orders are meant for creating the opportunity of other load operations on this atomic variable to sync with the write". That's what **release** does. Acquire, consume and acq_rel describe the *reading* side of a synchronizes-with pair, and a pure store reads nothing, so they have nothing to apply to.

### Quiz

**Q1 -> hit.** (b) is fully right:
- X: > "once we cross the pop, we make a heap allocation … if that happens, we lost the data as we have already popped it off the stack". Right.
- Y: > "the copy or move assignment operators can throw as we assign the local tmp to value … we have already popped". Right.
- The principle is in your X answer: > "thats no problem since we havent popped it off the stack yet".

(a) has three slips, noted but not counted:
- > "the data empty check wont throw". That line **throws `empty_stack`**. It's safe because nothing has been modified yet.
- > "move itself doesnt move anything, it just converts the data.top() into an Rvalue". True of the `std::move` cast, but it isn't why this is safe. The move *constructor* does run inside `make_shared`, and it can throw. It's safe because `data.pop()` hasn't run yet.
- Not listed among the non-throwing operations: returning `res`.

Rule to state outright next time: **nothing that modifies the stack happens until every operation that can throw has finished; the only modifying step, `data.pop()`, comes last and can't throw.**

**Q2 -> hit.**
- (a) > "thread A already has the lock audit_m … thread A is waiting for the retries mutex". Right: A holds `audit_m` and waits for `m` inside `empty()`; B holds `m` inside `pop()` and waits for `audit_m` inside the move constructor.
- (b) Right: a single thread deadlocks on itself. Strictly, it's UB to lock a `std::mutex` you already own (3.3.3), and in practice that means it hangs.
- (d) Right: it's the user's responsibility. The book's reason is simpler than yours: you can't add an item to a stack or remove one without copying it or allocating memory for it, so user code under the lock is unavoidable.

**(c) has two errors.**
- > "`value=std::move(data.top());` … will run the move constructor". It runs **`operator=(order&&)`**, the move assignment. The snippet logs "assigned" there for exactly this reason. I compiled it in scratch to check: the log shows `assign`.
- > "`threadsafe_stack(const threadsafe_stack& other)` will run the assignment operator of order, i.e., `order& operator=(order&& o)`". Copying a stack copies its elements, so it needs `order`'s **copy** operations. `order` declares a move constructor, so its copy constructor and copy assignment are implicitly deleted, and the copy constructor won't compile for `threadsafe_stack<order>` (checked in scratch: "use of deleted function `order& order::operator=(const order&)`"). It also runs under `other.m`, not `m`.

Also missing from (c): **allocation** (`operator new`) in `push()` and in `make_shared`, which the book lists explicitly.

**Q3 -> hit.**
- (a) Both parts are right: `pop()` re-checks `empty()` under the lock and throws, and `pop()` returns the value instead of having a separate `top()`.
- (b) > "the user must guarantee that while the constructor and destructor run, no other method of that object must be run on any other thread". That's the guarantee. Two things to fix:
  - The book's reason it's acceptable is missing: an object is constructed once and destroyed once, and calling members on a half-constructed or half-destroyed object is never sensible, concurrent or not.
  - > "easy to ensure with … call_once and once_flag or an atomic flag". Those aren't the tools for this. You ensure it by not sharing the stack until it's fully constructed, and by joining or stopping every user before it's destroyed.
  - Also "the constructors" (plural): the copy constructor counts too.
- (c) Right: nobody else can reference an object under construction, and `other` must be locked so it can't change while it's being copied.

### Build

None. 6.2.1 triaged to quiz only. The optional `pop()`-by-move + throwing-type extension wasn't taken up.

### Queue updates

```
python3 tools/review.py log 4.1-notify-outside-lock hit --note "6.2.1 warm-up: predicate checked under m; producer needs m to push, so no notify can fall in the check-to-sleep gap"
python3 tools/review.py log 5.2-cas-update-loop hit --note "6.2.1 warm-up: load/store loses 1200; CAS loop with v>expected re-check can't; weak's spurious failure just retries (question wording ambiguous - mine)"
python3 tools/review.py log 5.2-memory-order-by-operation miss --note "6.2.1 warm-up: per-op lists right; named only acquire + acq_rel as disallowed for clear()/store (missed consume); reason inverted"
python3 tools/review.py add 6.2 6.2-stack-exception-ordering "Listing 6.1 pop(): every throw point (lock, empty_stack, make_shared allocation / move ctor; pop(T&): move assignment) and why each is safe - nothing is modified until the no-throw data.pop(). Spot a variant that pops before its last throwing step and say what's lost." --kind core --source "lock-based-concurrent-data-structures-notes.md 6.2.1 Q1"
python3 tools/review.py add 6.2 6.2-user-code-under-lock "Which user code runs while the stack's lock is held (push: move ctor + allocation; pop(): allocation + move ctor; pop(T&): move ASSIGNMENT; copy ctor: element copies under other.m), the two deadlocks it enables (lock-order inversion with a user mutex; re-entering the same stack), and why the book leaves it to the user." --kind core --source "lock-based-concurrent-data-structures-notes.md 6.2.1 Q2"
```

Results: `4.1-notify-outside-lock` streak 2 (due 10-20); `5.2-cas-update-loop` streak 1 (due 10-13); `5.2-memory-order-by-operation` streak 0 (due 10-10). No quiz misses, so no miss items. 6.2.1 closes with two core items. Q1 and Q2 carry the slips from (a) and (c). Q3's constructor/destructor contract is already covered by `6.1-ctor-dtor-copy-swap-contract`.

### Fix list

No rows opened or closed.

### Beyond the book (ungraded)

Left blank. Answer: if the move constructor throws partway through, the top element is still on the stack, but some of its members may already have been moved out. It's left in a valid but unspecified, half-moved state, so "the stack is unmodified" holds for its *size*, not necessarily for the element's *value*. `std::vector` avoids this during reallocation with `std::move_if_noexcept`. It moves if the move constructor is `noexcept` (or if there's no copy constructor), and copies otherwise, so a throw leaves the original intact.

---

## 6.2.2 — A thread-safe queue using locks and condition variables (Listings 6.2, 6.3)

```
$ python3 tools/review.py due   (2026-10-08)
Due now: 69 active (68 overdue) | maintenance due: 1 | due in next 3 days: 7 | total active: 80, retired: 1
WARM-UP PICKS (3):
  1. [miss] 3.3-map-concurrent-write-unsafe  (sec 3.3, 11d overdue, streak 0)
     source: alternative-facilities-for-protecting-shared-data-notes.md 3.3.2 Q3 (Correction 2026-10-05)
  2. [miss] 4.2-packaged-task-conversion-rule  (sec 4.2, 11d overdue, streak 0)
     source: waiting-for-one-off-events-with-futures-notes.md 4.2.2 Q10 (Correction 2026-10-05)
  3. [miss] 4.2-shared-future-per-thread-copy  (sec 4.2, 11d overdue, streak 0)
     source: waiting-for-one-off-events-with-futures-notes.md 4.2.5 Q20 and 4.2.1 Q2 (Corrections 2026-10-05)
Also due (next worksheets): 66 more (5.1-memory-locations-bitfields, 4.2-future-mechanism-discrimination, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 11 (F1-F11)
Lint: 33 items more than 7 days overdue (review-only paused during ch6, per the 2026-10-07 plan)
```

Triage:
- What's new compared with your 4.1 `threadsafe_queue`: Listing 6.2 is the same class, except every pop **moves** out of `front()` (yours copies) and there's no copy constructor. Book typo: `push()` reads `data_queue.push(std::move(data));`. It should be `new_value`; as printed it doesn't compile.
- The exception twist: `push()` calls `notify_one()`, the one woken waiter throws in `make_shared` inside `wait_and_pop()`, and the other waiters are never woken. The book's three fixes and their costs: **code-tracing** (Q1).
- Why `wait_and_pop()` adds no new races or deadlocks; why one mutex around a `std::queue` limits concurrency, and what changes once you control the implementation: **recall** (Q2).
- Listing 6.3, the queue that stores `std::shared_ptr<T>`: allocation moves into `push()` outside the lock, and the shared_ptr pops can no longer throw. It's a reusable mechanism, and the 9.1 thread pool reuses it, so it's a **build**: `threadsafe_ptr_queue` (below). Not quizzed separately.
- Not quizzed: copy vs. move in the pops (trivial), and the fine-grained queue the last paragraph points to (that's 6.2.3).

Add a low/med/high confidence tag to each quiz answer.

### Warm-up

1. A trading system keeps its symbol registry in a plain `std::map<std::string, venue_info>`, with no lock. Reader threads only call `find()` on symbols that are already listed. One admin thread occasionally `insert`s a newly listed symbol that no reader ever looks up. A teammate says: "The writer and the readers never touch the same key, so there's no race." Why is that wrong for `std::map` specifically? Say what the insert changes that a reader looking up a *different* key also depends on, then state the general rule for one writer and concurrent readers on a standard container.

Answer (med): as we learned in the topic about using thread ids in containers as keys, we discussed what different containers needed from the thread id objects to make them work. there we learned that std::map is a sorted associative container which requires the operation < to be defined and requires its operations to have logarithmic complexity. An almost ubiquitous underlying implementation for std::map is a balanced binary tree - a red black tree and upon insertion or deletions, if the tree nodes break the rules then it uses rotations to fix the node structure. This is what leads to the answer of the question of as to why, even if readers only query existing keys and not newly inserted ones, its unsafe until the entire operation is complete, its because on every new insert there is the possibility of a reordering happening which breaks the invariants of the data structure to other external viewers until the operation or function returns, things like single or double rotations involve promoting child nodes to parents, exchanging children and so on, which means that until the rebalance operations are complete, the rules or invariants which make a binary tree traversal work are broken and even reads are unsafe or liable to be corrupted since the link that might lead to a particular key might be broken during rebalancing operations. we know that for thread safety to be established, no threads but the operating thread ought to see broken invariants at one time and this necessitates the use of a mutex to achieve thread safety.


2. You wrap `float scale(int lots)` in a `std::packaged_task<double(double)>`. For this to compile, two separate conversions must be possible. For each one, say which side of the call it belongs to (what goes in, what comes out) and which type has to convert to which.

Answer (med): the rules were:
- The callable's result type must be implicitly convertible to the packaged_task's specified return type.
- For the arguments, the supplied task arguments must be implicitly convertible/usable to satisfy the callable's parameters.


3. Two pricing threads both need the result of one `std::async` calibration run.
   (a) Describe a setup in which both threads call `.get()` on the *same* `std::shared_future` object. Is it safe?
   (b) Give the safe pattern.
   (c) Why does that pattern need `std::shared_future` rather than `std::future`? Name the property one has and the other lacks.

Answer (med): 

(a) calling get() on the same std::shared_future object is a race condition as the shared_future objects themselves are not synchronized or ordered. Instead, the paradigm is to give their copies to other threads such that each that can call get() on their own copy of shared_future and not race on the original future object.

(b) the safe pattern could look something like this:
```cpp
std::future<int> f = std::async(find_answer_to_ltuae);
auto sf = f.get_future().share();
std::thread t1([sf](){
   int res = sf.get();
   std::cout<< res <<std::endl;
});
std::thread t2([sf](){
   int res = sf.get();
   std::cout<< res <<std::endl;
});
t1.join();
t2.join();
```
as you can see, we gave each thread a copy of the shared future and that way both threads didnt race on the original shared_future object.

(c) we could not do the above safe pattern using just std::future because std::future cannot be copied, only moved so we couldnt pass them as copies to the threads t1 and t2. we could pass the future by reference but that would mean the threads could race on the get() call and two calls to get will lead to a future error, where as if we wanted to avoid that we would have to `move` the future object into one of the threads and the other would be deprived. So as you can see, no matter how we slice this, there isnt a way to achieve the above pattern with a simple std::future.

### Quiz

1. (Code-tracing) Three strategy workers C1, C2 and C3 are blocked in Listing 6.2's `std::shared_ptr<T> wait_and_pop()` on an empty queue. The feed thread pushes one snapshot X, and C1 is the waiter `notify_one()` wakes.

```cpp
std::shared_ptr<T> wait_and_pop()
{
    std::unique_lock<std::mutex> lk(mut);
    data_cond.wait(lk,[this]{return !data_queue.empty();});
    std::shared_ptr<T> res(
        std::make_shared<T>(std::move(data_queue.front())));
    data_queue.pop();
    return res;
}
```

   (a) C1 throws. Name the line and the two things on it that can throw. Afterwards, what's in the queue, is `mut` locked, and what are C2 and C3 doing?
   (b) Five seconds later the feed thread pushes snapshot Y, and nothing else throws. Trace what happens. What's the lasting effect on the queue and its waiters until the burst ends?
   (c) Give the two fixes the book names that keep `std::queue<T>` storage (the third is your build). For each, say where the change goes and what it costs.

Answer (med):

(a) if C1 throws, its most likely going to be on the line ` std::shared_ptr<T> res(std::make_shared<T>(std::move(data_queue.front())));`. it could be that the system is overwhelmed and failed to do new space allocation on the heap, or perhaps the move constructor for T failed one of its steps. This isnt too bad though since the data_queue.front() value doesn't get popped off the queue unless we get a successful allocation on the heap, that said, it poses a different problem where now, the wait has relinquished its lock on the mutex, the thread has been put to sleep and the element that was meant to be popped off the queue has been left in there, forgotten and obscure. C2 and C3 will remain asleep because the feed thread called notify_one() not notify_all().

(b) with the next snapshot pushed to the queue another notify_one() call will wake one of C1, C2, C3. In a no throw scenario, one of them will wake, its wait will acquire lock on the mutex, find that the predicate is true, make the heap allocated shared pointer and pop that element off the queue before returning the shared pointer to the caller. the lasting effect on the queue will be that the element has been moved over to a heap allocated Lvalue and its former Lvalue has been popped off the queue. As for the effect on the waiters? there is none, i.e., while all this happens, the other waiters will remain asleep and will not contend for the lock since the push to the queue also needs the lock to hit another notify to wake one of them up.

(c) the two fixes are:
- have a notify_one() call in exception handling path to wake another waiter thread that should retry the heap allocation and pop. the issue is that if that too throws, then we are back to the square one.
- the other fix is to replace notify_one() calls with notify_all() calls so that all threads get awoken and contend for the mutex until the queue is empty but the cost is the overhead of waking the lot of them unnecessarily when they check to see that the queue is empty after all after the first one or two threads try.


2. (Recall)
   (a) The book says the `wait_and_pop()` functions add no new race conditions or chances of deadlock, and that the invariants still hold. Give its two reasons, both about the state of the queue and the mutex at the moment `data_cond.wait()` returns.
   (b) Even with several threads blocked in different member functions, only one thread can do work on this queue at a time. The book says part of that limit comes from using `std::queue<>`. What does it mean by "one data item that's either protected or not", and what does taking control of the implementation allow instead?

Answer (med): 

(a) wait_and_pop() functions make use of std::unique_lock and std::condition_variable::wait(). the operation of wait is such that we supply it a predicate which checks if the underlying structure is empty() or not before going forward with popping an element. the use of a single mutex across all operations including these two of the threadsafe_queue avoids most deadlock scenarios. when the notify() gets called and the wait() functions do wake up the threads, they acquire lock or contend to acquire lock, check the predicate, and if it returns true, they return with the mutex locked, otherwise they relinquish their lock and put the thread back to sleep, so no matter what, there is no operation that they do or the code below them in the current implementation does, that happens outside mutex lock and that prevents any data races from happening. there is also no top() and pop() or similar race window and though there is one between empty() and the other operations, wait_and_pop() have the empty() checks on the underlying std::queue built in to the predicates which once again prevent any race conditions there. the unique_lock RAII bolt on allows for flexibility needed by wait() and also ensures mutex doesnt remain locked when going out of function scope. Interface design also exposes no invariants or race windows out of mutex lock.

(b) there are two reasons which arent mutually exclusive necessarily, which are causing the serialization of all the threads, i.e., the use of a single mutexes on all the methods / operations and the interface of std::queue<T> which is the underlying data structure. using more than one mutexes with the interface of the std::queue<> object might apparently allow for more concurrent operation by threads but it just opens the queue up to all sorts of deadlocks and race conditions, the single mutex here  is acting as a synchronization vehicle that prevents concurrent method access for the thread safety of the structure and we loose that safety the moment we introduce more than one mutexes, for the different operations. so consider you use one mutex for push and another for pop(), instant possible race condition interleaving right there, since push and pop from two different thread could obviously happen on the same front() memory() location. the point is, thread safety rules which are more important to even get the structure working before concurrency questions are considered, just dont hold up as soon as we start making basic changes to the current implementation like adding more than one mutex. the interface encapsulation of std::queue<> just doesnt allow for more granular control needed to enable more concurrency opportunities safely.

### Build task — `threadsafe_ptr_queue` (Listing 6.3)

Folder: `ch06_designing_lock_based_concurrent_data_structures/threadsafe_ptr_queue/` (scaffolded: CMake files, root `add_subdirectory`, and empty header and driver). It's a new target, not an edit of the 4.1 queue, so `threadsafe_queue` stays as it was reviewed. The header and class have a different name because `verify.sh` puts every `include/` folder on the include path. You may start from your own 4.1 header; the book stays closed.

Interface:

```cpp
template<typename T>
class threadsafe_ptr_queue {
public:
    threadsafe_ptr_queue();
    threadsafe_ptr_queue(const threadsafe_ptr_queue&) = delete;
    threadsafe_ptr_queue& operator=(const threadsafe_ptr_queue&) = delete;

    void push(T new_value);
    bool try_pop(T& value);
    std::shared_ptr<T> try_pop();
    void wait_and_pop(T& value);
    std::shared_ptr<T> wait_and_pop();
    bool empty() const;
};
```

Keep a `size()` if your tests want one.

Thread-safety contract:
- Every member is safe to call concurrently from any number of threads, except the constructor and destructor (the 6.1 contract).
- Internally the queue stores `std::shared_ptr<T>`, not `T`.
- `push()` creates the stored `T` **before** taking the lock. `wait_and_pop()` sleeps on the condition variable rather than polling. `try_pop()` never blocks.
- The two `shared_ptr`-returning pops never copy, move or allocate a `T`, so nothing in them can throw because of `T`.
- The two `T&` pops move-assign into `value`. If that assignment throws, the item stays at the front of the queue.
- No item is lost or delivered twice.

Tests to write:
1. **Conservation under mixed pops.** 4 producers each push 10,000 distinct ints (producer `p` pushes `p*10000 + k`). 4 consumers run at the same time, one per pop flavour: `wait_and_pop()` returning a pointer, `wait_and_pop(T&)`, and `try_pop()` in each form, called in a loop. After joining the producers, push four `-1` stop values; each consumer stops at the first `-1` it gets. Assert that, across all consumers, every one of the 40,000 values was received exactly once. *Catches:* lost or duplicated items, and races (TSAN).
2. **Pointer pops never touch `T`.** Use a test type whose copy and move operations (constructors and assignments) all throw while a global flag is set. Push two items with the flag off, then turn it on. Call `try_pop()` and `wait_and_pop()` (the pointer versions). Assert that neither throws and that they return the two items in FIFO order. *Catches:* a pop that still builds the `T` itself (the Listing 6.2 shape).
3. **A throwing `T&` pop keeps the item.** Same type. Push one item with the flag off, turn the flag on, and call `try_pop(T&)`. Assert that it throws. Turn the flag off and call `try_pop(T&)` again. Assert that it succeeds with that same item. *Catches:* popping from the queue before the assignment into `value` has finished.
4. **Construction happens outside the lock.** Use a type whose move constructor sleeps 200 ms. Thread A calls `q.push(slow_type{})`. Pass a temporary, so the only move is the one inside `push()`. The main thread waits about 50 ms, then times one `q.empty()` call. Assert that the call returns in well under 50 ms. *Catches:* creating the stored element while holding the mutex.

Self-explanation (2–4 sentences, closed-book, before review): why is this queue thread-safe and exception-safe? What breaks with the obvious alternative of storing `T` and creating the `shared_ptr` inside `wait_and_pop()`, and why is that worse with `notify_one()` and several waiters?

Answer:


### Beyond the book (optional, ungraded)

The book says that storing `std::shared_ptr<T>` makes `wait_and_pop()` "safe again". Look at Listing 6.3's `wait_and_pop(T& value)`: after the wait, it does `value=std::move(*data_queue.front());` while holding the lock. Can that line throw? If it does, with three waiters and `notify_one()`, are you back in Q1's situation? What could the `T&` overload do about it? Not queued unless you say "queue it".

Answer:


---

## Review: 6.2.2 (2026-10-08)

Confidence tags are on every warm-up and on quiz Q1 and Q2, all med.

### Warm-up

- **3.3-map-concurrent-write-unsafe -> hit.** > "on every new insert there is the possibility of a reordering happening … single or double rotations involve promoting child nodes to parents … the link that might lead to a particular key might be broken during rebalancing". That's the mechanism: an insert re-links tree nodes that lookups for *other* keys walk through. The general rule is there too: other threads must not see broken invariants, so you need a mutex. Stated as the rule itself: any write concurrent with any other access to the container needs synchronization; only reads-with-reads are safe.
- **4.2-packaged-task-conversion-rule -> hit.** > "The callable's result type must be implicitly convertible to the packaged_task's specified return type … the supplied task arguments must be implicitly convertible … to satisfy the callable's parameters." Both directions are right. You didn't apply them to the case asked: parameter side `double` -> `int`; return side `float` -> `double`. Next time, name the actual types.
- **4.2-shared-future-per-thread-copy -> hit.**
  - (a) > "calling get() on the same std::shared_future object is a race condition". Right. A concrete setup: both thread lambdas capture `sf` by reference.
  - (b) Each lambda captures `sf` by value, so each thread gets its own copy. Right.
  - (c) > "std::future cannot be copied, only moved". Right.
  - Nit: `f.get_future().share()` doesn't compile. `std::future` has no `get_future()`; that belongs to `std::promise` and `std::packaged_task`. It's `f.share()`, or `std::shared_future<int> sf = std::async(...);`.

### Quiz

**Q1 -> miss.**
- (a) The line is right: > "std::shared_ptr<T> res(std::make_shared<T>(...)) … failed to do new space allocation … or perhaps the move constructor for T failed". So is the queue state: X is still there. The mutex and C1 are wrong:
  - > "the wait has relinquished its lock on the mutex, the thread has been put to sleep". C1 is **not** asleep. `wait()` had already returned with the lock held. The exception leaves `wait_and_pop()`, and `lk`'s (`unique_lock`'s) destructor unlocks `mut` during unwinding. C1 is now in whatever code catches the exception, not waiting. C2 and C3 still sleep: right.
- (b) This is the probed point, and it's missing. > "the lasting effect on the queue will be that the element has been moved over … As for the effect on the waiters? there is none".
  - The push of Y wakes one waiter, say C2. The queue is FIFO, so C2 pops **X**, not Y. Y stays in the queue, and C3 sleeps on with an item waiting.
  - The lasting effect: the queue is one item behind its waiters. Every later push wakes one waiter, who takes the item *before* it. So each snapshot is delivered one push late (stale data in a burst), and the last item stays stranded until some push happens while no one is asleep, or until a `try_pop` takes it.
- (c) Both fixes are right: `notify_one()` again on the exception path, and `notify_all()` with its cost (most woken threads find the queue empty and go back to sleep). > "if that too throws, then we are back to the square one". Not quite: the next waiter's exception path also notifies, so the hand-off keeps going until a waiter succeeds or none are left.

**Q2 -> hit.**
- (a) Both reasons are there: > "they return with the mutex locked" and > "wait_and_pop() have the empty() checks … built in to the predicates". When `wait()` returns, the queue is non-empty and the data is still under the lock.
- (b) > "the interface encapsulation of std::queue<> just doesnt allow for more granular control needed to enable more concurrency opportunities safely". That's the point. Stated in the book's terms: a `std::queue` is one item that you either lock as a whole or don't lock at all, and you can't put the head and the tail under different locks. Writing your own implementation allows **finer-grained locking** (6.2.3).
- Nit: > "push and pop … on the same front() memory location". `push()` works at the back. The two ends only meet when the queue has 0 or 1 items, which is the problem 6.2.3 solves.

### Build

`threadsafe_ptr_queue: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK` (4 + 16 runs, about 8 s per 4).

**Library: correct.** Storage is `std::queue<std::shared_ptr<T>>`. `push()` builds the pointer before taking the lock. The pointer pops move the `shared_ptr` out of the queue, which can't throw. The `T&` pops move-assign before `pop()`. A quick check in scratch confirmed that both `T&` pops leave the item in the queue when the assignment throws.

**Driver, against the Tests-to-write list:**
- Test 1 (conservation, 4 pop flavours): written. Counting each value and asserting exactly one of each can fail. ✔
- Test 2 (pointer pops never touch `T`): written. A pop that builds the `T` would throw, which ends the program, and the values and FIFO order are checked. ✔
- Test 4 (construction outside the lock): written. It passes a temporary, so only the move inside `push()` sleeps, and the `< 50 ms` assert can fail. ✔
- **Test 3 can't fail.** Checked: I made a copy of your header that pops *before* assigning, and your driver still prints all four PASSED. Three reasons:
  1. Nothing asserts that `try_pop(T&)` threw. If it doesn't throw, "TEST 3 PASSED" still prints.
  2. `throwing_type third(0);` is declared twice. The `third` inside `try` hides the outer one and goes out of scope before the `catch`, so the `catch` checks the outer `third`, which was never passed to `try_pop`. That `third.value == 0` is always true.
  3. The second half is missing: turn the flag off, call `try_pop(T&)` again, and assert it returns item 3. That's the part that catches the bug.

  -> **F12.**
- Notes, not fix items: Test 2's move constructor takes `const throwing_type&&`. It still counts as a move constructor, but it can't modify the source; harmless here. The move assignment's message says "copy assignment".
- Nits: unused `<atomic>` and `<thread>` in the header, and `<future>`, `<random>` and `<mutex>` in the driver; a stray `;` after the constructor body.

**Self-explanation: not written** (the Answer is blank). It's part of F12. The build isn't done until it's in.

### Queue updates

```
python3 tools/review.py log 3.3-map-concurrent-write-unsafe hit --note "6.2.2 warm-up: insert can rebalance the red-black tree (rotations re-link nodes other lookups walk through); any write concurrent with reads needs a lock"
python3 tools/review.py log 4.2-packaged-task-conversion-rule hit --note "6.2.2 warm-up: both directions right (task args -> callable params; callable result -> task return); stated as rules, not applied to double->int / float->double"
python3 tools/review.py log 4.2-shared-future-per-thread-copy hit --note "6.2.2 warm-up: same shared_future object from two threads = race; copy per thread; future is move-only. Nit: wrote f.get_future().share() (future has no get_future; it's f.share())"
python3 tools/review.py add 6.2 6.2-queue-stranded-waiter "Listing 6.2 wait_and_pop() + notify_one(): the woken waiter throws in make_shared (allocation or T's move ctor) -> item stays queued, unique_lock's destructor unlocks during unwinding, the thrower leaves the function (not asleep), the others stay asleep. Next push wakes one waiter, which takes the OLD item (FIFO); the queue stays one item behind its waiters until a push finds no sleeper." --kind miss --source "lock-based-concurrent-data-structures-notes.md 6.2.2 Q1"
python3 tools/review.py add 6.2 6.2-queue-exception-fixes "The book's three fixes for the notify_one() + throwing waiter problem, and each one's cost: notify_all (wakes everyone, most go back to sleep); notify_one() again on the exception path in wait_and_pop; store shared_ptr<T> so the pointer pops can't throw (the T& pops still move-assign)." --kind core --source "lock-based-concurrent-data-structures-notes.md 6.2.2 Q1(c)"
python3 tools/review.py add 6.2 6.2-ptr-queue-alloc-outside-lock "Listing 6.3: storing shared_ptr<T> moves the allocation + T construction into push(), BEFORE the lock (6.2 had to do it under the lock in pop). Same number of allocations; the gain is shorter mutex hold time. The cost lands on the pushing thread." --kind core --source "lock-based-concurrent-data-structures-notes.md 6.2.2 threadsafe_ptr_queue build"
```

Results: the three warm-ups are at streak 1, due 10-13. `6.2-queue-stranded-waiter` (miss) is due 10-09; the two core items are due 10-10.

### Fix list

- **Opened F12:** `threadsafe_ptr_queue`. Test 3 needs an assert that the throw happened, no shadowed `third`, and the flag-off retry that asserts item 3 comes back. The self-explanation is also owed. Re-verify with `verify.sh` and check that Test 3 now fails on the pop-before-assign version (I'll do that check).

### Beyond the book (ungraded)

Left blank. Answer: yes, `value=std::move(*data_queue.front());` runs `T`'s move assignment, and that can throw, as 6.2.1 said of `pop(T&)`. The item stays in the queue, because `pop()` comes after. But with `notify_one()` and several waiters, you're back in Q1's situation for that overload. The book's "safe again" holds for the `shared_ptr`-returning `wait_and_pop()` only. Fixes for the `T&` overload: catch, call `notify_one()`, and rethrow; or take the `shared_ptr` out and pop it under the lock, then do the assignment after unlocking. The second fix means that if the assignment throws, the item is lost from the queue, so you'd push it back or accept losing it.

---

## 6.2.3 — A thread-safe queue using fine-grained locks and condition variables (Listings 6.4–6.10)

```
$ python3 tools/review.py due   (2026-10-09)
Due now: 70 active (66 overdue) | maintenance due: 1 | due in next 3 days: 7 | total active: 83, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.1-data-race-definition  (sec 5.1, due today, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.1 Q3 gave 2 of 3 conditions (missed 'at least one not atomic'))
  2. [miss] 5.1-memory-locations-bitfields  (sec 5.1, 12d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.1 Q2 said 4 (correct is 3); Q1 missed 'adjacent')
  3. [miss] 4.2-future-mechanism-discrimination  (sec 4.2, 11d overdue, streak 0)
     source: waiting-for-one-off-events-with-futures-notes.md 4.2.3 Q16 (Correction 2026-10-05)
Also due (next worksheets): 67 more (5.2-atomic-flag-special, 5.2-atomic-flag-static-init, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 12 (F1-F12)
Lint: 37 items more than 7 days overdue (review-only paused during ch6, per the 2026-10-07 plan)
```

Triage:
- What's new: the queue is your own singly linked list with a dummy node, `head_mutex` and `tail_mutex`, instead of one mutex around a `std::queue`.
- Why two mutexes on the plain list (Listing 6.4) don't work: on an empty queue `push()` touches both `head` and `tail`, and with one item `push()` and `try_pop()` both touch the same `next` pointer. The dummy node fixes this, at the cost of storing data by pointer. **Code-tracing** (Q1).
- Where the locks go in `pop_head()`: `get_tail()` must run inside the `head_mutex` lock (the book's broken version runs it outside), and `get_tail()` needs `tail_mutex` even for a one-pointer read. **Code-tracing** (Q2).
- `wait_and_pop()` design: notify after unlocking, the predicate `head != get_tail()` under `head_mutex` only, why `wait_for_data()` returns the lock, and why there's no deadlock. **Recall** (Q3).
- Listings 6.7–6.10 are a reusable mechanism (and the basis for ch7's lock-free queue), so they're a **build**: `fine_grained_queue` (below). The `T&` pops assigning *before* unlinking is tested in the build, so it isn't quizzed.
- Book typo: Listings 6.7 and 6.10 declare `void empty()`, but the body returns a bool. It's `bool empty()`.

Add a low/med/high confidence tag to **every** quiz answer.

### Warm-up

1. A colleague reviews this code, where two threads run `tick()` at the same time:

```cpp
std::atomic<int> fills{0};
int rejects = 0;
void tick(bool ok) {
   if (ok)
      fills.fetch_add(1); 
   else 
      ++rejects; 
}
```

   She says: "`fills` is fine because the atomic operations serialize the two threads' accesses; `rejects` is the bug." Using the Standard's definition of a data race, list **all** the conditions that make two accesses a data race. Then say which condition `fills` fails to meet, and why her reason ("serialize") isn't that condition.

Answer (med): First I shall enumerate the conditions that constitute a race condition:
- two threads access a single memory location concurrently.
- at least one of the operations is a write operation.
- there is no enforced ordering between the operations.
- at least one of the operations is not atomic.

Now lets take a look at the code, i can, from quick analysis with an A/B threads access framework i have developed over the course of doing these, discern two major scenarios for race conditions.

- the first and most obvious race condition - the serialization or happens before relation that could have happened between fills and rejects, does not happen because of the if-else setup that we have in the code, i.e., both are clearly disjoint and have no association between each-other, not even the sequential happens before that could have been argued if there was no if/else setup. With that said any and all threads can access tick() with false and write to rejects with no mutual memory order enforcement or atomicity among their writes, fulfilling all the aforementioned conditions of a data race.

- the next is a bit of a hypothetical scenario or shall i say, a tangent point of discussion, its that what if we removed the if/else and we had just the two statements `fills.fetch_add(1);  ++rejects;`. Now one might assume and assume correctly that if the fetch_add is presented with a write with which it can synchronize, then it will, and that does synchronize access to rejects in that scenario, however, there in lies the issue, this is for when there is a write from above or elsewhere to `fills` that fetch_add can sync with, otherwise, we are open to race conditions again even if we have no if/else, because the potential happens before chain between rejects in other functions and this one, in this hypothetical situation would rely on the happening of a write to `fills` after the operation to rejects in those functions for the happens before to be honored in this function and that need not always be the case.

2. A trading system tags each tick with this struct:

```cpp
struct tick_tag {
    int      venue_id;
    unsigned is_bid   : 1;
    unsigned is_stale : 1;
    double   px;
    unsigned is_halted : 1;
};
```

   (a) How many memory locations does a `tick_tag` have? Name what's in each one.
   (b) State the rule that decides this, including the word that matters for bit-fields.
   (c) One thread sets `t.is_bid` while another sets `t.is_stale` on the same object, with no lock. Is that a data race? Why or why not?

Answer:


3. The strategy thread sends an order over the venue's WebSocket and wants a future it can wait on for "order 42 acknowledged". The ack arrives later as a message the network thread reads off the socket. Which of `std::async`, `std::packaged_task` and `std::promise` fits? For each of the other two, say specifically why it doesn't fit this case.

Answer (med): oh this one ought to be a bit easier, the obvious answer is that `std::promise` is more suitable. Now the reason is that not a callable need be run to set a future's value associated with a promise and that means we can handle waiting for event and then setting the value in one while(true) worker thread. See, no matter how we slice this, at least one thread has to be there in a busy wait loop, listening to the object holding the connection for incoming messages, so there is at least one thread constantly running a core. Now how many others we commit to this activity of setting the future's value is dependant on us because say we use std::async, then we'd have to make a callable and send it over to another thread which will then based on the incling message, set the value of the future, and there is the other issue of, when spawning a future for an async, we cant just get a future first and not spawn the async task immediately, unless we make a strange contraption where we instantiate a future, and then we make it into a shared future and then have another future then get() the value from this async and before we do that we move the original future into this one, I am fuzzy on how to make it work and I do think I could make it work although its easy to see the point i am trying to make and it's that std::async isnt designed for situations like the one described in the question. Moving on to std::package, it could be made to work, i.e., we take a future off of it and put the packaged_task with a callable in it on a map against the order id which we can later the retrieve using incoming message id and then run the callable in that packaged_task which itself can just be a function that returns the value or the incoming message and thats all the callable has to do, however, we can see that its all this extra code that need not be there when we can just set the value instead of making a local packaged task, to then extract the callable and run it, we could just set the value in the promise in a one to two line setup. So, async is ill suited, packaged_task can be made to work but lots of boilerplate code and promise is perfectly catered to problems where external events are in the picture, such as waiting on a server to send acknowledgement messages as the question states.


### Quiz

1. (Code-tracing) Take Listing 6.4 (plain singly linked list: `std::unique_ptr<node> head`, `node* tail`, no dummy node). A colleague adds `head_mutex`, locked by `try_pop()` around its use of `head`, and `tail_mutex`, locked by `push()` around its use of `tail`.
   (a) The queue holds exactly one item. Thread A calls `try_pop()` while thread B calls `push()`. Name the object both threads touch without a common lock, and which line in each function touches it.
   (b) Why can't `try_pop()` simply check whether it's in this one-item case and take `tail_mutex` only then?
   (c) The queue is empty. Which of the two pointers does `push()` modify, and what does that force?
   (d) How does the dummy node remove the problem in (a)? What does it cost the data layout?

Answer (med):

(a) if thread A was calling the try_pop() and thread B was calling push() then we have a situation where both could be accessing the same single Node N and its next pointer N->next. both the `unique_ptr<node> head` and the `node* tail` pointers are pointing to N, so we have a situation where in try_pop() the following lines are trying to access the node N under head using the lines `std::unique_ptr<node> const old_head = std::move(head); head = std::move(old_head->next)`. similarly push could be accessing the same node N under the tail pointer using the line, `if(tail){tail->next = std::move(p)}`.

(b) the issue is that simultaneous operation of both push() and try_pop() could result in an interleaving where try_pop() could walk over the end of the list or tail, even if we use individual mutexes for head and tail in the pop() and push() functions respectively. Of course the solution in reality is what the question says, i.e., take tail_mutex and check if the one node situation exists. In the solution to this problem, we use a dummy node always in the list and the check is that if both the head and tail pointers point to this dummy node and if so, the try_pop() returns earlier, the thing to keep in mind though is that, the tail pointer check mutex has to be held within the scope of and while the head_mutex is held. The reason for this is that if the tail pointer is obtained under tail_mutex before the head mutex is taken for the pop() then we get a race window in between the two locks where another thread could take the same route and end up locking head_mutex before the current thread and do its pop() using the same old tail pointer which makes the tail pointer stale and it could be that the list became empty and we have a pop() trying to walk off the edge of the list or trying to delete in a list with no nodes.

(c) in case the queue is empty, the push has to modify both head and tail pointers in listing 6.4. This forces both head and tail pointers to point to the same node and exposes the setup in listing 6.4 to a race condition. to avoid the race condition, to avoid race condition while pushing to an empty queue, the function would need to acquire both head_mutex and tail_mutex, this however causes serialization or concurrency loss.

(d) the design of this structure is such that the head is where all the pops take place and the tail is where all the pushes take place so the head moves forward and the tail moves forward in one direction and both can operate independently of eachother using their individual head and tail mutexes; until and unless they overlap their operations over the same node in the situation where there is only one node in the queue, at which point, both try to alter it using different pointers to the same node as we have discussed above. The dummy node is a remedy and an invariant, i.e., there will always be a dummy node in the empty queue and it will hold no data but is meant purely as a boundary for both types of operations. So, with the dummy node installed, each pop() checks, while taking tail_mutex for a short time, if the node its about to pop() is indeed the tail or dummy node and if it is, then we get an early return. The push operation unlike before where it handled both head and tail, now acts only on the tail node which invariably points to the dummy node and it pushes the new data to the old dummy node and simply declares the new node instance after it attaches it at the end, the new dummy node. So in this way, any possible overlaps and data races are avoided and the cost is the overhead associated with having the list always carry a dummy node and the additional tail_mutex lock needed to be acquired in every call to a pop() function, though its only held for a small duration so its well worth the overhead when compared against the simplicity it avails to functions that need be written for the concurrent queue.

2. (Code-tracing) The book's broken `pop_head()`:

```cpp
std::unique_ptr<node> pop_head()      // broken
{
    node* const old_tail = get_tail();          // locks tail_mutex briefly
    std::lock_guard<std::mutex> head_lock(head_mutex);
    if (head.get() == old_tail)
        return nullptr;
    std::unique_ptr<node> old_head = std::move(head);
    head = std::move(old_head->next);
    return old_head;
}
```

   (a) Thread A calls `get_tail()` and is then held up before it gets `head_mutex`. Describe what other threads (pushers and poppers) do in the meantime so that, when A finally runs, `old_tail` is no longer in the list. What does A's comparison do then, and what happens to the queue?
   (b) In the correct version, `get_tail()` runs while `head_mutex` is held. Why can't the same thing happen there? Use the book's point about which way `tail` moves.
   (c) `get_tail()` reads one pointer. Why does it still need `tail_mutex`? Give the book's reason, which is about the data `try_pop()` reads from the node, not only about `tail` itself.

Answer (med): get_tail() has the following code:
```cpp
node* get_tail(){
   std::lock_guard<std::mutex> tail_lock(tail_mutex);
   return tail;
}
```

(a) this scenario is why the above code, with get_tail() outside the head_lock, is broken. the reason is that between the tail pointer returning and thread A acquiring head_mutex's lock under headlock, we have a race window, where, other threads may win the race and acquire the lock before thread A, this means that if there was only one valid item in the queue, then another thread may have already deleted that node before thread A can once again get the lock for doing its pop(). Now in such a a scenario, where another thread has already deleted the node thread A was working to delete, thread A should fail the check in `if (head.get() == old_tail)`, however, this is the broken part, thread A now holds the old and stale tail pointer because it could have happened that a nen node was pushed to tail and was promoted and two nodes were deleted, so that when thread A checks the if condition, it fails to see that head is indeed equal to the tail. so with the stale and invalid check passed, thread A tries to remove the dummy node and promote the null, next pointer to be the new head which leads to invalidation of the queue and UB.

(b) If we try to do the threads A and B test then even if multiple push and pop threads are operating, we dont get a stale check the is cause for concern, i.e., say get_tail is now inside the head_lock's scope, we take lock on tail_mutex and return a tail pointer, now while our head is being popped, some other thread, upon the return of get_tail(), could also operate on the tail and make our tail pointer for comparison stale but not in a way thats damaging because it would only add a node and make the tail go forward and the head upon deletion also moves forward, so in a way, the purpose of having the tail comparison node being fetched inside the head_lock isnt to prevent concurrent pushes to the tail, its to avoid races among concurrent pops where stale comparisons may lead to head pointer trying to go over the tail pointer and lead to race condition or UB. in a nutshell, even if get_tail() call returns a pointer that might go stale with regards to new pushes, it allows us to avoid pointer that's stale with regards to the new pops of nodes getting removed and us going over the edge of the Linked List.

(c) the reason it is important for get_tail() to use a mutex is that without it, we get a race condition where a concurrent push might be happening while we call get_tail() that could lead to a bad / torn read, or data race. 


3. (Recall) About the waiting version (Listings 6.8–6.10):
   (a) `push()` calls `data_cond.notify_one()` after the block that holds `tail_mutex` has ended. What's the gain, according to the book?
   (b) The wait condition is "queue not empty". What predicate does `wait_for_data()` use, and why does the wait need only `head_mutex` and not both mutexes?
   (c) Why does `wait_for_data()` return its `std::unique_lock` to the caller instead of letting it unlock at the end of the function?
   (d) Two mutexes, and `pop_head()`'s caller holds both at once. Why can't this queue deadlock?

Answer (med):

(a) To be specific, the gain is reduced time under contention for acquiring the same mutex lock on the wait() function side. If the notify() signal is sent while the mutex is held on the push function, then that means the notify() line is inside the lock scope and that the lock was held on the notify() side, but since the notify has been called, the wait() has woken up the thread and now has started to contend for the mutex_lock which remains locked on the notify() side until its scope runs out and its unlocked, whereupon which the wait will acquire the lock on its side using its predicate (get_tail check in predicate also needs tail_mutex) - this was a small window of wasted contention time which could have been avoided, had the notify() been placed outside of the lock's scope, then the wait() could have acquired that lock on its side immediately and thus would have saved the time wasted under contention for acquiring that lock over the shared mutex.

(b) the wait performs the get_tail() check in its predicate `data_cond.wait(head_lock, [&]{return head.get() != get_tail();});`, but notice, on each wakeup, and the first call, wait acquires the lock on head mutex first, before acquiring and releasing the lock on tail_mutex. Thus, the statement in this part of the question isnt technically accurate, i.e., for a short time, like the try_pop() which has no wait(), we acquire both mutexes for a short time together - head_mutex is held throughout the major part of all pop() functions where as within that lock, get_tail() is called and its tail_mutex is invariably held for a short time and released in every pop() function call.

(c) for the same reason we call, get_tail() inside the scope of head_lock, to avoid allowing race windows to other pop() functions which results in stale references and UB. using unique_lock is often great because one can lock and unlock it mid function and also `move` it between scopes with the underlying mutex still locked.

(d) because the mutexes are acquired and released in a fixed order and done so consistently across all function calls for all threads acting on the overall queue object. What I mean by that is - unlike operation= methods or swap() methods where objects of the same class are passed and the order in which their mutexes get called can be mixed or non-discernable across the different thread calls, in the pop() methods, we `know` that the two mutexes are of `this` very object and so, locking `tail_mutex` withing the scope of the `head_mutex` lock is not going to get mixed up or altered no matter how many threads call the pop() functions. This is the reason we can get away with nested locks in this instance without having a deadlock.  

### Build task — `fine_grained_queue` (Listings 6.7–6.10)

Folder: `ch06_designing_lock_based_concurrent_data_structures/fine_grained_queue/` (scaffolded: CMake files, root `add_subdirectory`, and empty header and driver). The class is `fine_grained_queue<T>`, so it doesn't clash with the other two queues. Book closed. You can reuse your own test types from `threadsafe_ptr_queue`'s driver.

Interface:

```cpp
template<typename T>
class fine_grained_queue {
public:
    fine_grained_queue();
    fine_grained_queue(const fine_grained_queue&) = delete;
    fine_grained_queue& operator=(const fine_grained_queue&) = delete;

    std::shared_ptr<T> try_pop();
    bool try_pop(T& value);
    std::shared_ptr<T> wait_and_pop();
    void wait_and_pop(T& value);
    void push(T new_value);
    bool empty();
};
```

Private helpers are up to you; the book uses `get_tail`, `pop_head`, `wait_for_data`, `wait_pop_head` and `try_pop_head`.

Thread-safety contract:
- A singly linked list of nodes (`std::shared_ptr<T> data`, `std::unique_ptr<node> next`). `head` owns the chain; `tail` is a raw pointer to the last node. There is always one **dummy node**: an empty queue has `head == tail`, both pointing at it.
- Two mutexes. `head_mutex` guards `head`; `tail_mutex` guards `tail`. Every read of `tail` from the pop side takes `tail_mutex` and happens while `head_mutex` is already held. That's the only place both are held, always in the order head, then tail.
- `push()` allocates the new `T` and the new node **before** taking `tail_mutex`, and calls `notify_one()` **after** releasing it.
- `wait_and_pop()` waits on the condition variable with `head_mutex`, not by polling. `try_pop()` never blocks waiting for data.
- The `T&` pops move-assign into `value` **while the node is still at the head**. If that assignment throws, the item stays at the front.
- The popped node is destroyed after `head_mutex` is released.
- Every member except the constructor and destructor is safe to call concurrently. No item is lost or delivered twice. FIFO order holds.

Tests to write:
1. **Conservation and per-producer order under mixed pops.** 4 producers each push 10,000 distinct ints (producer `p` pushes `p*10000 + k`, k counting up). 4 consumers run at the same time, one per pop flavour, and stop at their first `-1`; push four `-1`s after joining the producers. Assert:
   - every one of the 40,000 values was received exactly once, across all consumers;
   - **within each consumer's list**, values from the same producer appear in increasing order.

   *Catches:* lost or duplicated items, broken linking (out-of-order delivery), and races on `head`/`tail` (TSAN).
2. **A throwing `T&` pop leaves the item at the head.** Use a type whose move and copy operations throw while a global flag is set. Push items 1 and 2 with the flag off, then turn it on. Call `try_pop(T&)` and assert it throws. Then call `wait_and_pop(T&)` and assert it throws (the queue isn't empty, so it won't block). Turn the flag off and call `try_pop(T&)` twice: assert you get 1, then 2. *Catches:* unlinking the node before the assignment into `value`. Make sure each assert can actually fail: check that the throw happened, and use the value you passed in.
3. **A push doesn't wait for a slow pop.** Use a type whose **move assignment** sleeps 200 ms (its move constructor stays fast). Push one item. Thread A calls `try_pop(T&)`; that assignment runs while `head_mutex` is held. The main thread waits about 50 ms, then times one `push()` of a new item (pass a temporary). Assert the push returns in well under 50 ms. *Catches:* `push()` sharing a mutex with the pop side (the single-mutex design), or taking `head_mutex`.

Self-explanation (2–4 sentences, closed-book, before review): why can `push()` and `try_pop()` run concurrently on this queue without a data race? Mention the dummy node and the `tail_mutex` read inside `head_mutex`. What breaks with the obvious alternative: no dummy node, or `get_tail()` called before locking `head_mutex`?

Answer: I will be brief since all the question points have been discussed at length above. push and pop can operate concurrently because both operations operate on the opposite ends of the queue, i.e., all pushes happen at the tail and all pops happen on the head. the dummy node avoids the situation where assuming there was only one node and no dummy node in the list, both push and pop methods would try to access it using their own head and tail pointers to the same node, though this is not a complete deal breaker but without a dummy node, it would require us to hold both mutex locks for both operations in a check condition which serializes majority of the access to the queue. A worse problem can occur where the a bunch of pop methods might have a race condition among themselves, if the get_tail() function is kept outside the head mutex lock, there forms a race window between both both locks which leads to possibility of other pops happening within the window and this makes the get_tail() pointer reference stale and if there was only one element in the queue, then its just invalid, and in this scenario we get a situation where the pop method tries to pop the head and move the head past the tail - leading to UB. The dummy node and the get_tail call being kept under head_mutex's lock - are solutions to the aforementioned edge case related to an empty queue where push and pop operations stand a chance of overlapping operations on the same memory addresses or same node.

A comment about my own implementation of the fine_grained_queue, i know i have not made a DRY implementation, I dont have many helpers, and I repeat code often. The reason the code was written this way is that i found the book's example harder to follow and more complex than necessary, writing DRY code at the expense of clarity seems like a bad idea when learning or making learning material and I dont think it makes much of a difference performance wise either. So, I like being explicit and clear and even if that means the code and comments get a bit verbose then so be it.


### Beyond the book (optional, ungraded)

You want `size()` on this queue without making every call take both mutexes. One option is a `std::atomic<std::size_t>` counter that `push()` increments and the pops decrement. Where exactly would you put the increment and decrement relative to the locks? What can a caller of `size()` observe that a caller of `empty()` can't, for example `size()` returning 0 while `empty()` returns false, or the reverse? Not queued unless you say "queue it".

Answer:


---

## Review: 6.2.3 (2026-10-11)

Confidence tags: all med, on warm-ups 1 and 3 and on every quiz answer. Warm-up 2 and the self-explanation are blank.

### Warm-up

- **5.1-data-race-definition -> miss.** The definition is finally complete: > "two threads access a single memory location concurrently / at least one of the operations is a write / there is no enforced ordering between the operations / at least one of the operations is not atomic." That's the first time "at least one not atomic" has been in the list.
  - The question then asked two things, and neither was answered.
    - **Which condition `fills` fails:** "at least one access is not atomic". Both accesses are `fetch_add`, so it can't be a data race, whatever the ordering.
    - **Why "serialize" isn't the reason:** legality comes from the accesses being atomic, not from any ordering they impose. Even `memory_order_relaxed` operations, which impose no ordering, can't race.
  - What the answer did instead is the old model. The tangent says the `fetch_add` "does synchronize access to rejects", which credits the atomic with protecting the plain int. In the hypothetical `fills.fetch_add(1); ++rejects;` run by two threads, `++rejects` is a data race, full stop.
  - Suggested correction, one line: *"fills: every access is atomic, so the 'at least one non-atomic' condition fails; ordering is irrelevant."*
- **5.1-memory-locations-bitfields -> miss.** Left blank.
  - (a) **4**: `venue_id`; {`is_bid`, `is_stale`}; `px`; `is_halted`.
  - (b) Every object of scalar type is its own memory location, and a **sequence of adjacent bit-fields** shares one. The `double` breaks adjacency, so `is_halted` is separate.
  - (c) **Yes, it's a data race.** `is_bid` and `is_stale` are the same memory location, so two unsynchronized writes race.
- **4.2-future-mechanism-discrimination -> hit.**
  - Promise: > "not a callable need be run to set a future's value". Right; the network thread sets the value when the ack arrives.
  - Packaged_task: > "could be made to work ... lots of boilerplate". Right: it ties the result to calling a function, and here there's no function to call, only a value that arrives.
  - Async: right in spirit. It launches a function and the future gets its return value, so it would have to block inside that function waiting for the ack.
  - Nit: > "at least one thread has to be there in a busy wait loop". A thread reading a socket blocks in the read; it doesn't spin.

### Quiz

**Q1 -> miss.**
- (a) Right: > "N->next ... `head = std::move(old_head->next)` ... `tail->next = std::move(p)`".
- (b) Missed. > "Of course the solution in reality is what the question says, i.e., take tail_mutex and check if the one node situation exists." The question's idea doesn't work. To know you're in the one-item case, `try_pop()` must read `tail`, which means locking `tail_mutex` on **every** call. Then `push()` and `try_pop()` both lock `tail_mutex` every time, and you're no better off than with one mutex. The rest of (b) is Q2's material (`get_tail()` inside the head lock).
- (c) Right: > "push has to modify both head and tail ... would need to acquire both head_mutex and tail_mutex".
- (d) The mechanism is right: > "push ... now acts only on the tail". The cost isn't: > "overhead associated with having the list always carry a dummy node and the additional tail_mutex lock". The book's cost is the **extra indirection**: a node holds `std::shared_ptr<T>`, not `T`, because the dummy node has to exist without a `T` in it.

**Q2 -> hit.**
- (a) > "a new node was pushed to tail and was promoted and two nodes were deleted, so that when thread A checks the if condition, it fails to see that head is indeed equal to the tail ... thread A tries to remove the dummy node and promote the null next pointer to be the new head". Right. `old_tail` names a node that's already been removed (deleted), so `head == old_tail` is false even when head is the dummy, and A moves head off the end of the list.
- (b) > "it would only add a node and make the tail go forward ... head ... also moves forward". Right: while `head_mutex` is held nobody else moves head, and tail only moves away, so head can't pass the value read.
- (c) Missed, though not counted against the grade. > "a concurrent push might be happening while we call get_tail() that could lead to a bad / torn read". That covers only the `tail` pointer. The book's reason: `push()` writes the old tail node's `data` and `next` under `tail_mutex`. Locking the **same** mutex in `get_tail()` puts the two calls in a defined order, so `try_pop()`'s later reads of `head->data` and `head->next` aren't a data race. Two different mutexes alone would give no ordering. This is now core item `6.2-tail-mutex-orders-node-data`.

**Q3 -> hit.**
- (a) > "the wait() could have acquired that lock ... (get_tail check in predicate also needs tail_mutex)". Right: the woken thread's predicate locks `tail_mutex`, so if `push()` still held it, the waker would block at once.
- (b) Predicate `head.get() != get_tail()`: right. Your pushback on my wording is fair: `tail_mutex` is taken briefly inside `get_tail()`. The point is that the wait's own lock is `head_mutex`, because the comparison needs only `tail`'s value, and `get_tail()` reads it under its own lock.
- (c) > "to avoid allowing race windows to other pop() functions". Right: the same lock is held from "not empty" through the pop, so no other popper can empty the queue in between.
- (d) > "locking tail_mutex within the scope of the head_mutex lock is not going to get mixed up". Right: it's the only place both are held, always head then tail.

### Build

`fine_grained_queue: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK` (4 + 16 runs, about 8.5 s per 4). A scratch check confirmed both `T&` pops leave the item at the head when the assignment throws, and the next pops return 1 then 2.

**Library: correct**, with one gap against the contract:
- **The popped node is destroyed while `head_mutex` is held.** In every pop, `old_head` is a local declared after the lock guard, so it's destroyed first, under the lock. In the `T&` pops, that also destroys the `T`. Nothing is unsafe, but it's the concurrency gain the contract lists: deletion should happen after the head lock is released. -> F13(a).
- Note: the `T&` pops **copy**-assign rather than move-assign. Your comment's reason is sound: a throwing move can leave the queued `T` half-moved. The cost is that those two members need a copyable `T`; a move-only `T` still works for the pointer pops. Accepted as a deliberate choice.
- Note: the `private:` line is commented out, so `head`, `tail`, the mutexes and the helpers are all public.
- Nits: unused `<atomic>`, `<queue>` and `<thread>` in the header, and `<future>`, `<random>` and `<mutex>` in the driver; the commented-out destructor (the default one is fine); `slow_type` is unused.

**Driver, against the Tests-to-write list:**
- Test 1: conservation is written, and its asserts can fail. **The per-producer order assert is missing.** -> F13(b).
- **Test 2 is not the listed test.** What's there is 6.2.2's Test 2 (pointer pops never touch `T`). The listed one is missing: a throwing `try_pop(T&)` and `wait_and_pop(T&)` each assert that they threw, then with the flag off you get 1, then 2. -> F13(c).
- Test 3: written, and the assert can fail. ✔
- **Self-explanation: blank.** -> F13(d).

### Queue updates

```
python3 tools/review.py log 5.1-data-race-definition miss --note "6.2.3 warm-up: FIRST time all conditions listed incl. 'at least one not atomic'; but never said fills fails that condition, nor why 'serialize' isn't it (tangent on fetch_add syncing rejects instead)"
python3 tools/review.py log 5.1-memory-locations-bitfields miss --note "6.2.3 warm-up: left blank"
python3 tools/review.py log 4.2-future-mechanism-discrimination hit --note "6.2.3 warm-up: promise (value set by the network thread on the ack); async ties result to a launched function's return; packaged_task needs a callable wrapper for no reason. Nit: reading a socket isn't a busy-wait"
python3 tools/review.py add 6.2 6.2-two-mutex-list-dummy-node "Plain linked-list queue with head_mutex + tail_mutex fails: on an empty queue push() sets both head and tail; with one item push() writes tail->next and try_pop() reads head->next (same node). try_pop can't 'check for one item first' because that needs tail, i.e. tail_mutex on every call -> same mutex as push, no gain. Dummy node keeps head and tail on different nodes; cost: data stored by pointer (extra indirection)." --kind miss --source "lock-based-concurrent-data-structures-notes.md 6.2.3 Q1"
python3 tools/review.py add 6.2 6.2-get-tail-inside-head-lock "Why pop_head() reads tail (get_tail) only while holding head_mutex: outside it, other pushes/pops can move past the old tail, the stale comparison fails on a really-empty queue and head is moved past tail off the list. Inside it, no one else moves head, and tail only moves away, so head can't pass the value read." --kind core --source "lock-based-concurrent-data-structures-notes.md 6.2.3 Q2(a)(b)"
python3 tools/review.py add 6.2 6.2-tail-mutex-orders-node-data "get_tail() locks tail_mutex not just to read the tail pointer: push() writes the old tail node's data and next under tail_mutex, and locking the same mutex gives try_pop a defined order with that push, so reading head->data/next isn't a data race (different mutexes alone would give no ordering)." --kind core --source "lock-based-concurrent-data-structures-notes.md 6.2.3 Q2(c)"
```

Results: the two 5.1 items are back to streak 0 (due 10-12), and `4.2-future-mechanism-discrimination` is at streak 1 (due 10-15). The new miss item is due 10-11 and the two core items 10-12. 6.2.3's quiz part closes with those items, and 6.2 is now `done` in the ledger, with its builds' open fixes in F12 and F13.

### Fix list

- **Opened F13** (`fine_grained_queue`): (a) destroy the popped node after `head_mutex` is released; (b) Test 1's per-producer order assert; (c) the listed Test 2 (throwing `T&` pops keep the item at the head); (d) the self-explanation.

### Beyond the book (ungraded)

Left blank. Answer: increment the counter in `push()` **after** linking the node, and decrement it in the pops **after** unlinking, both inside their respective locks. `size()` then never counts an item a popper can't yet reach. Because the counter and the list are updated under different locks, `size()` is only a snapshot. It can briefly be 0 while `empty()` says false (just linked, not yet counted), but never nonzero with nothing reachable. The exact behaviour depends on where you put the updates, which is why `std::` containers don't offer a lock-free `size()` for concurrent use.
