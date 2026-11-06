# 6.3 — Designing more complex lock-based data structures

## 6.3 intro, 6.3.1 (lookup table, Listings 6.11–6.12), 6.3.2 (thread-safe list, Listing 6.13)

```
$ python3 tools/review.py due   (2026-10-12)
Due now: 80 active (75 overdue) | maintenance due: 1 | due in next 3 days: 5 | total active: 87, retired: 1
WARM-UP PICKS (3):
  1. [misconception] 5.1-data-race-definition  (sec 5.1, due today, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.1 Q3 gave 2 of 3 conditions (missed 'at least one not atomic'))
  2. [misconception] 5.3-sync-needs-value-read  (sec 5.3, due today, streak 1)
     source: synchronizing-operations-and-enforcing-ordering-notes.md 5.3.1-5.3.2 Q1(b)(c)
  3. [miss] 5.2-atomic-flag-special  (sec 5.2, 14d overdue, streak 0)
     source: 2026-09-24 migrated from section ledger (prior: 5.2.1 Q1 missed 'basis for building other atomics' and conflated lock-free with single instruction)
Also due (next worksheets): 77 more (5.2-atomic-flag-static-init, 5.2-last-one-out-counter, 5.2-rmw-result-not-reload, ...)
Maintenance due (spare slot only): 2.1-terminate-vs-system-error
Open fix-list items: 13 (F1-F13)
Lint: 60 items more than 7 days overdue (review-only paused during ch6, per the 2026-10-07 plan)
```

Triage:
- **Scene-setting (6.3 intro, start of 6.3.1):** a lookup table has more operations than a stack or queue and is usually read far more than it's modified. Folded into Q1.
- **Interface design:** std::map's iterators are the biggest problem; the add race is avoided by combining add and change; there are three ways to report "not present". **Recall** (Q1).
- **Choosing the structure:** a tree or sorted array won't allow fine-grained locking; a hash table with a fixed bucket count allows a lock per bucket, and `get_bucket()` needs no lock. **Recall** (Q2).
- **Exception safety of Listing 6.11**, case by case. **Code-tracing** (Q3).
- **6.3.2 opening:** why there are no STL iterators, why the list's own `for_each` breaks a chapter 3 guideline, and what's left to the user. **Recall** (Q4).
- **Listings 6.11–6.13 are reusable mechanisms, so they're a build.** Per your proposal: `threadsafe_list` (6.13) in its own target, and `threadsafe_lookup_table` built with `threadsafe_list` buckets (below). Hand-over-hand locking and `get_map()`'s locking order are covered by the build and its self-explanation, so they aren't quizzed.
- **Book code slips** (relevant to the build):
  - Listing 6.13's destructor passes `[](node const&){return true;}` to `remove_if`, but the predicate is called with `*next->data`, which is a `T`. It should take `T const&`.
  - Listing 6.12 writes `buckets[i].mutex`, but `buckets` holds `unique_ptr`s, so that's `->`. It also reaches into `bucket_type`'s private members.
  - Listing 6.11's `find_entry_for` is `const` but returns a non-const iterator.
  - `boost::shared_mutex` / `boost::shared_lock` are `std::shared_mutex` / `std::shared_lock` in C++17.
- **Warm-up 3 is trimmed to the book:** the book only distinguishes "done with atomic instructions" from "done with an internal lock", so the "lock-free vs. single instruction" half of that item isn't graded.

Add a low/med/high confidence tag to **every** quiz answer.

### Warm-up

1. Each pair below is two threads touching the same variable, with no other synchronization unless stated.

   - (i) `std::atomic<long> hits;` Thread A: `hits.fetch_add(1);` Thread B: `long h = hits.load();`
   - (ii) `int seq;` Both threads run `++seq;` while holding the same `std::mutex`.
   - (iii) `int last_px;` Thread A: `last_px = 101;` Thread B: `int p = last_px;` No lock.
   - (iv) `std::atomic<bool> halted;` Thread A: `halted.store(true, std::memory_order_relaxed);` Thread B: `bool h = halted.load(std::memory_order_relaxed);`

   First, state the definition of a data race with every condition. Then, for each pair, say whether it's a data race and which condition decides it. Pair (iv) gives no ordering at all between the two threads. Does that change your answer for (iv)? Why or why not?

Answer:

A data race is when:
- two threads try to access the same memory location at the same time (concurrently).
- at least one of the operations is non atomic.
- at least one of the operations is a write.
- there is no enforced ordering between the operations, i.e., there is no happens before relationship between the operations.

(i) `hits` is an atomic variable and the operations happening on it have the default seq_cst ordering on them which means the threads A and B have to obey a global ordering of these operations, so my verdict is that its not a data race.

(ii) without `std::mutex` this would have definitely been a race, however when its written that both hold the same mutex, im lead to believe that this `++seq` line is inside a function where both threads are getting serialized by the same mutex and so this is not a data race.

(iii) this is definitely a race condition because all conditions that define a data race are observable here, non atomic operations on a non-atomic variable, no enforced ordering since we are running different threads so no sequenced before, we have a write operation among them, i.e., `last_px = 101;`, so all these to say, yes this is a data race.

(iv) This is not a race condition because even though there is no enforced ordering, hence the relaxed tag, the operations are still atomic operations working on an atomic type so there is going to be no UB or data corruption or torn reads happening and while not all threads might see the updates on the `halted` variable immediately, they all will respect the overtime modification order on it, all this to say, its not a data race. had it been a non atomic variable, then it would probably be a data race scenario in my opinion.

2. The feed thread publishes a quote; the strategy thread reads it.

```cpp
struct quote { int bid; int ask; };
quote q;                                   // plain struct, not atomic
std::atomic<bool> ready{false};

// feed thread
q.bid = 100;
q.ask = 101;
ready.store(true, std::memory_order_release);

// strategy thread
int b = q.bid;                                       // (1)
if (ready.load(std::memory_order_acquire)) {
    int a = q.ask;                                   // (2)
} else {
    int a2 = q.ask;                                  // (3) "just take whatever's there"
}
```

   For each of reads (1), (2) and (3): is it ordered after the feed thread's writes to `q`? Name the relation that is there, or the one that's missing. Where it's missing, what does the program have?

Answer: the scenario given in the question is incomplete in a sense, the ready.store(release) and the ready.load(acquire) pairs are there, however, the logic gates are setup such that, only in the scope of the if statement in the strategy thread do we get a happens before - synchronization relationship such that a is going to get 101 from q.ask in there, in else, a2 might or might not get 101 depending on how things or inter-leavings play out, though there isnt any guarantee.

Here is the kicker or twist, the way the code is setup, unless we add a statement before this `while(!ready.load(std::memory_order_acquire)){}` then if the strategy thread runs before the feed thread's ready.store runs, then there is no synchronization getting set up in which the strategy thread can partake, i.e., for any synchronization to take place in the above code, not that there is much going on even if it happens, the ready.store() has to run exactly before  the ready.load() runs for that synchronization to take place between them. so even in the ideal scenario, (1) and (3) have no guaranteed happens before relation with the feed thread and (2) does have happens before relation with feed thread thanks to acquire-release cross thread synchronization which is unlikely to materialize without a while loop blocking to check it. 


3. A teammate says: "`std::atomic_flag` is just a stripped-down `std::atomic<bool>`. The smaller interface is the only thing worth knowing about it."
   (a) Give the two ways the book says `std::atomic_flag` is special among the atomic types.
   (b) Every other atomic type has `is_lock_free()`. Why doesn't `atomic_flag`? And when `is_lock_free()` returns false for some other atomic type, what does that tell you about how its operations are carried out?

Answer:

(a) std::atomic_flag is special because first, its the only atomic type which is guaranteed to have a lock free implementation at the granular, CPU instruction level and this is guaranteed by the C++ standard itself regardless of all hardware, i.e, exotic embedded systems, flight controllers, mainstream x86 CPUs, whatever, this is a lock free implementation. The other thing that makes it unique is that it does not have a non-modifying read, i.e., test_and_set and clear are atomic_flag's the only methods and test_and_set() returns the old value before setting the flag to true. another unique aspect is that this is the one type that must be initialized with INIT_ATOMIC_FLAG enum, unlike other atomic types which one can initialize with Rvalues of their relevant normal types, like atomic<bool> can be initialized using true/false.

(b) atomic_flag is guaranteed to be lock_free by the C++ standard and so it doesnt have nor does it need a `is_lock_free()` check method. the other atomic types might or might not have an atomic CPU instruction depending on if they are integral types like int or float or long, and also depending on their size relative to a CPU word or two CPU words on a higher end. For user defined types that typically exceed 65 bits, there are no singular / atomic CPU instructions for copying, comparing, storing and loading them, so the CPU uses a lock / internal mutex around the bunch of CPU instructions needed to atomicize the different operations for that type. this doesnt mean that type's operations are not atomic, they are, thanks to the internal lock, however, they are as a result no longer lock free and so `is_lock_free()` returns false.

### Quiz

1. (Recall) The interface of a thread-safe lookup table.
   (a) The book sets aside `std::map`'s interface. What's the biggest problem with it from a concurrency point of view? What would such an object have to cope with if another thread modifies the container?
   (b) Two threads each add a value for the same new key. With separate "add" and "change" functions, what goes wrong? What interface change does the book suggest, and where have you already seen it?
   (c) "Obtain the value associated with a given key, *if any*." Give the book's three ways of telling the caller the key isn't there.

Answer:

(a) the reason for setting aside std::map interface and reimplementing the map is because the std::map is heavily reliant on the concept and usage of the iterator. An iterator is best described as an external handle to the internals / data objects held by the container and can be used to traverse the container's objects as well as modify the container on a per object basis, i.e., iterators can help one modify or delete the entry they point to, or help add one next to their own object's position in the container. Having iterators in many of the std library containers allow for the different algorithm functions like for_each and if_any, etc, to be ubiquitously applied to all the containers, this means that the for_each definition need not be different for std::vector and std::map, since its going to be using iterator to move across whatever container is passed to for_each and the actual implementation of for_each need not concern itself with whats in the container, only that an iterator will help traverse the container in an expected "front-to-back" fashion. This works great in a single threaded code base but when dealing with multiple threads with all kinds of possible inter-leavings, we have the issue that iterators might be pointing to positions or objects which another thread might delete, effectively turning them into dangling pointers. To avoid this a solution might be to have mutex locks to whatever object the iterator points to, but this opens the code up to deadlocks because the authority over that mutex lies external to the container, since the iterator lifecycle is also external to the container and by extension that of the mutex held by the iterator and so common issues like trying to re-lock that mutex or not having done so while exiting that container's callable, there are all sorts of possible deadlock creating inter-leavings which become plausible with lock supported iterators.

(b) the combination of add_or_update is similar to how we combined top and pop into just a pop() function that returns the top value in a stack, and similarly for queues that we have made. the race such combinations avoid is what i would generalize as the race window between look and act, where we first check if something is there before we act on it, that gap between these two is a race window in the multithreaded context because if another thread comes in between these two actions and deletes that node or entry then we have a race condition or UB. In this case, if we consider the two "add" and "change" functions, why are they separate? well the former considers that a particular entry doesn't exist in the container, where as the latter works on the assumption that the particular entry does exist in the container. With that said for a user to choose which of these two to use, the user would first need to check if the entry exists in the container or not and thus introducing the race window I talked about earlier. So, add_or_update just takes this race window away by checking the existence of the entry for us and then deciding if it needs to add or update said entry.

(c) the three possible ways to express if the telling the caller the key isn't there:

- returning a default value which could be explicitly provided or just be a part of the container class which would indicate the absence of the key if returned.
- returning a part<mapped_type, bool> where the boolean returned false would be indicative of the key-value-pair not existing in the map.
- there is also the mention of returning a smart pointer to the data, which if nullptr, means the KVP wasnt in the map, though this is a bit context dependant on wether we want the lists at each bucket storing the objects or the keys with smart pointers or smart pointers alone, etc.


2. (Recall) Choosing the data structure.
   (a) Why does a binary tree give little room for fine-grained locking? Why is a sorted array even worse? One reason each.
   (b) In Listing 6.11, `get_bucket()` reads `buckets` and calls the hasher without taking any lock. What property of the design makes that safe? Name one change to the class that would break it.
   (c) With a `shared_mutex` per bucket, how much does concurrency improve compared with one `shared_mutex` for the whole table, according to the book? What downside does the book name? How does making `Hash` a template parameter help with it?

Answer:

(a) binary tree give little room for concurrency, even if fine-grained locking is used because if we rule out the use of iterators then about every operation in binary trees like insert, delete, find, etc. all these need to pass through the root node, which means that all threads trying for concurrent operation on the binary tree need to serialize on the root node no matter what, this can of course be improved with fine grained locking as compared to one mutex for whole tree, however, there is still the serialization at the root node which is not going away.

A sorted array is worse because you cant in advance know where the desired value is going to be in the array and so one needs to lock the entire array for one operation.

(b) what makes this safe is that the number of buckets is fixed as soon as the lookup table is constructed, this we can see in the threadsafe_lookup_table constructor where we supply the number of buckets are a parameter to the constructor. get_bucket's calculation is using the hashing function to procduce a number and that number is then used with the `fixed` size of the buckets vector, i.e., the modulus of the size and hashed number returns the index of the bucket whose list we ought to use to access or store the new entry. what would break the consistency of this system is if we allowed the size of the buckets vector to change, this would misinform us of the index, leading to wrong inserts or accesses, and break the structure.

(c) with a shared_mutex for each bucket instead of a shared mutex for the whole table, we get the opportunity to run threads on all buckets of the table simultaneously which is of course a huge improvement to just one entry / one thread at a time, now, no matter the operation, insert or get or delete, we can run N threads for N buckets at least. Furthermore, since each bucket runs a shared mutex and not a normal mutex, this means that we can run many concurrent readers on each bucket or one writer on each bucket, so that too is a gain in concurrency because that means that N threads is the least number of concurrent threads that we can run on the table. The book says using shared_mutex still isn't good enough considering we still would have only one writer thread on each bucket and we'd usually want better than just one. the std library provides std::hash<T> which allows us to hash any type T. For better distribution of the hashes produced across the available vector size, the std::hash<T> has been specialized to all common / primitive types like int, float, string, etc, however, providing hashing function as a template parameter gives the user the opportunity to get the similar regular / better distribution benefit - the std library gets by specializing std::hash for types like int, float, etc - for their own data type T if they happen to have a better function for hashing their particular type.


3. (Code-tracing) Exception safety of Listing 6.11. For each case, say whether it can throw. If it can, say what state the bucket is left in and why the book considers that acceptable.
   (a) `value_for()`
   (b) `remove_mapping()`
   (c) `add_or_update_mapping()` when the key is new
   (d) `add_or_update_mapping()` when the key already exists

Answer:

(a) if the find_if() function throws then nothing is modified and if the copy construction during return from bucket_type's value_for fails then still nothing got modified so no loss of information or broken state left for other threads.

(b) if the find_if() in find_entry_for() breaks or the iterator return copy throws, then until that point, no alternations or modifications to the data have been made. After this, data.erase() is called and it's guaranteed not to throw.

(c) if the key is new, the data.push_back() function for std::list gets called and that method is exception safe, i.e., if it throws, it doesn't alter the list and doesn't leave it in a broken state.

(d) if the key is not new then we are trying to assign the value to found->second. This is the user defined type and a failed assignment, depending on how user wrote the copy assignment operator, might or might not lead to leaving the old data in a broken state and its the user's responsibility to consider the possibility of a failed allocation, where by deletion operation or replacement of the old data for the `this` object in the method be placed with these things in mind.


4. (Recall) Iteration in the thread-safe list (the start of 6.3.2).
   (a) Why doesn't the list offer STL-style iterators? What would an iterator have to hold, and why is that a bad idea given who controls the iterator's lifetime?
   (b) The list provides `for_each` instead. Which chapter 3 deadlock-avoidance guideline does that break? What else does it hand to user code? What's the alternative to that, and what does it cost?
   (c) What does the book leave to the user, and why is that fine when the list is used by the lookup table?

Answer: 

(a) to keep it short, iterators are objects which hold pointers to internal objects / data of a container class and their lifetimes are entirely out of the container class's control. This means that for concurrent operations to work with iterators, the iterator needs to lock a mutex over whatever node or entry in the container it's referencing and this is problematic since the container cannot control the duration of this lock and since the lifetime of the lock held by the iterator is tied to the lifetime of the iterator and the iterator's lifetime is reliant entirely on the caller, this sets us up for possible deadlock scenarios.

(b) the deadlock avoidance guidance would be with regards to user supplied functions, i.e., avoid deadlocks by disallowing user to supply user defined functions. Now an example of a deadlock scenario is that a user supplies a function to for_each which in iself calls one of the list's methods which will undoubtedly need a lock on the one of the same mutexes which the for_each will lock in its hand over hand locking traversal and thus cause a deadlock. there is no alternative, i.e., we do need a for_each functionality for a list for it to be useful, nonetheless, the book puts the burden of responsibility on the user to avoid calling container functions in the supplied callables and to avoid creating situations for deadlock like locking mutexes in their callables and causing data races by storing the references for access outside the locks.

(c) as already mentioned, the book puts the burden of responsibility on the user to avoid calling container functions in the supplied callables and to avoid creating situations for deadlock like locking mutexes in their callables and causing data races by storing the references for access outside the locks. "why is that fine when the list is used by the lookup table?" because the book states that we know we arent going to do anything naughty and by this the book means that we are already versed in the dos and donts and so its unlikely for us to set up the aforementioned conditions for data races or deadlocks.


### Build task — `threadsafe_list` (Listing 6.13) and `threadsafe_lookup_table` (Listings 6.11–6.12, with `threadsafe_list` buckets)

This is your proposal, and it's what the book sets up: 6.3.1 ends by asking for finer-grained locking within a bucket, and 6.3.2 derives the list's operations from what Listings 6.11 and 6.12 need. The text never shows the two combined, though. Combining them raises three problems the book doesn't solve for you, listed in Part B's contract. Expect two sittings: the list and its driver first, then the table. Book closed for both.

Scaffolded: both folders with their `CMakeLists.txt` files (the table's target links `threadsafe_list`), the root `add_subdirectory` lines (list first), and four empty files: `threadsafe_list.hpp`, `threadsafe_list_driver.cpp`, `threadsafe_lookup_table.hpp`, `threadsafe_lookup_table_driver.cpp`. The table header includes `"threadsafe_list.hpp"`; `verify.sh` already puts every task's `include/` on the path.

#### Part A — `threadsafe_list` (Listing 6.13)

Interface:

```cpp
template<typename T>
class threadsafe_list {
public:
    threadsafe_list();
    ~threadsafe_list();
    threadsafe_list(const threadsafe_list&) = delete;
    threadsafe_list& operator=(const threadsafe_list&) = delete;

    void push_front(T const& value);
    template<typename Function>  void for_each(Function f);
    template<typename Predicate> std::shared_ptr<T> find_first_if(Predicate p);
    template<typename Predicate> void remove_if(Predicate p);

    // Plus any operations Part B needs. Your design; the book leaves
    // further operations "as an exercise for the reader".
};
```

Thread-safety contract:
- The list is singly linked. Each node has its own `std::mutex`, a `std::shared_ptr<T> data` and a `std::unique_ptr<node> next`. A default-constructed head node with no data starts the list.
- `push_front()` allocates the node and its `T` before taking any lock, then locks only the head node's mutex.
- Every traversal starts at the head and moves one way only, **locking the next node before unlocking the current one** (hand-over-hand). Any operation you add follows the same rule.
- `for_each()` and `find_first_if()` call the user's function or predicate while that node's mutex is held.
- `remove_if()` unlinks a node while holding both its lock and the previous node's lock. It destroys the node only after unlocking the node's own mutex.
- User functions must not take locks or keep references past the call (the book's rule). The list doesn't defend against that.
- The destructor removes every node. Every member except the constructor and destructor is safe to call concurrently.

Tests to write (list):
1. **Concurrent pushes and removals keep exactly the right items.** 4 pusher threads each `push_front` 10,000 distinct ints (pusher `p` pushes `p*10000 + k`). At the same time, 2 remover threads loop `remove_if(is even)` until the pushers finish. After joining everyone, call `remove_if(is even)` once more, then collect every value with `for_each`. Assert the result is exactly the 20,000 odd values, each once. *Catches:* unlinking a node without holding the previous node's lock, which loses or double-frees a node that's being pushed or removed next to it (TSAN or ASan also flags it).
2. **A slow node only holds up threads behind it.** Push three items so the list reads A, B, C from the head. Thread T runs `for_each` with a function that sleeps 200 ms when it reaches C. The main thread waits about 50 ms, then times `push_front(D)` followed by `find_first_if(== A)`. Assert both together return in well under 50 ms. *Catches:* one mutex for the whole list, or holding the head lock for a whole traversal.

#### Part B — `threadsafe_lookup_table` (Listings 6.11–6.12, buckets made of `threadsafe_list`)

Interface:

```cpp
template<typename Key, typename Value, typename Hash = std::hash<Key>>
class threadsafe_lookup_table {
public:
    using key_type    = Key;
    using mapped_type = Value;
    using hash_type   = Hash;

    explicit threadsafe_lookup_table(unsigned num_buckets = 19, Hash const& hasher = Hash());
    threadsafe_lookup_table(const threadsafe_lookup_table&) = delete;
    threadsafe_lookup_table& operator=(const threadsafe_lookup_table&) = delete;

    Value value_for(Key const& key, Value const& default_value = Value()) const;
    void add_or_update_mapping(Key const& key, Value const& value);
    void remove_mapping(Key const& key);
    std::map<Key, Value> get_map() const;
};
```

Thread-safety contract:
- The number of buckets is fixed in the constructor. A key's bucket is `hasher(key) % number of buckets`. Finding the bucket takes no lock.
- Each bucket is a `threadsafe_list` of key/value pairs.
- An operation on a key locks only nodes in that key's bucket, hand-over-hand, and **stops at that key's entry**. It never waits on a node further down the list.
- No lock covers a whole bucket in a way that makes `value_for`, `add_or_update_mapping` and `remove_mapping` exclude each other. Otherwise the per-node locks gain nothing. Test 4 checks this.
- **Problem 1, duplicate keys.** A key appears at most once in its bucket, even when several threads add the same new key at the same time. How you guarantee that is your design decision; explain it in the self-explanation.
- `remove_mapping()` removes the key's one entry and stops there. Test 1 relies on this to make a duplicate visible.
- **Problem 2, reading the value.** `value_for()` returns a copy of the value, or `default_value`. No code outside the table reads an entry except under the lock that protects it. This is chapter 3's rule: don't pass pointers or references to protected data outside the lock.
- **Problem 3, the snapshot.** `get_map()` returns a consistent snapshot: the table's contents at one moment, as if nothing else ran during the call. If it takes several locks, it takes them in one fixed order every time (the book's rule), so it can't deadlock.
- If `Value`'s own assignment throws, that's the user's problem (the book's position).
- Every member except the constructor and destructor is safe to call concurrently.

Tests to write (table):
1. **Many threads adding the same new keys never create a duplicate.** Use 1 bucket. 8 threads each call `add_or_update_mapping(k, thread_index)` for `k = 0..499`, all in the same order, released together. After joining, for each `k`: assert `value_for(k, -1)` is one of the thread indexes, then call `remove_mapping(k)` once and assert `value_for(k, -1) == -1`. *Catches:* "find, then insert" done as two separate steps. Two threads both miss the key and both insert it, and the duplicate survives one removal.
2. **Readers never see a half-written value.** Use 3 buckets and keys 0..9, all present at the start, with `std::string` values. 2 writer threads each loop 20,000 times, writing a 64-character string of a single letter (the letter changes each iteration) to key `i % 10`. 4 reader threads each loop 20,000 times on `value_for(i % 10)` and assert all 64 characters are the same letter. TSAN is the main detector. *Catches:* reading an entry through a pointer after its node is unlocked, while a writer changes it.
3. **`get_map()` is a snapshot of one moment.** Use the default 19 buckets. One writer thread adds keys 0..1,999 in increasing order (value = key). 3 threads call `get_map()` in a loop until the writer finishes. For every snapshot, assert that its keys are exactly `0..size-1` with no gaps, and that each value equals its key. *Catches:* a `get_map()` that reads the buckets one at a time without holding them all, so it can see a later key but miss an earlier one.
4. **A writer isn't held up by a slow node further down the bucket.** Use 1 bucket and your own key type: a struct with an `int id`, whose `operator==` sleeps 200 ms when **both** sides have id 2 and a global flag is set. Pass the table a small hash functor for it. Insert keys 1 and 2 so that key 1's entry comes before key 2's in the list. Which insertion order does that depends on where your table inserts. Set the flag. Thread A calls `value_for(key 2)`; the matching comparison sleeps while key 2's node is locked. The main thread waits about 50 ms, then times `add_or_update_mapping(key 1, new value)`. Assert it returns in well under 50 ms. *Catches:* a bucket-wide lock around the normal operations (with the book's `shared_mutex`, the writer waits for the reader), or an update that walks past its own key's entry.

Self-explanation (closed-book, before review, 3–5 sentences in total):
- **List:** why can't the list's operations deadlock with each other, and why may `remove_if` destroy a node after unlocking its mutex? What breaks if `for_each` unlocked the current node before locking the next one?
- **Table:** how does your table stop two threads from adding the same new key twice? Why can't `value_for` read the entry after its node is unlocked? How does `get_map()` capture a single moment without risking deadlock?

Answer:


### Beyond the book (optional, ungraded)

None this time. The build's three combination problems already go past the text.

---

## Review: 6.3 (2026-10-16)

No confidence tags on any answer this time (warm-ups or quiz). The self-explanation is blank.

### Warm-up

- `5.1-data-race-definition` -> **hit** (the first hit on this misconception).
  > "This is not a race condition because even though there is no enforced ordering, hence the relaxed tag, the operations are still atomic operations"

  That's the condition: both accesses are atomic, so a pair with no ordering at all is still not a data race. All four conditions are listed.
  - Residue: (i) is justified by "seq_cst ... global ordering". The ordering is irrelevant there too; what decides (i) is again that both accesses are atomic.
  - Nit: (iii) and (iv) say "race condition" where they mean "data race".
- `5.3-sync-needs-value-read` -> **miss.**
  > "(1) and (3) have no guaranteed happens before relation with the feed thread and (2) does"

  That part is right. But the question asked what the program has where the relation is missing, and the answer never says it. Instead, (3) is described as "a2 might or might not get 101". It's a **data race, so undefined behaviour**, not a read of a possibly stale value.
  - Also missing for (1): it's sequenced *before* the load, so even when the load reads `true`, nothing orders (1) after the feed thread's writes.
  - The `while` loop tangent doesn't change any of the three answers.
- `5.2-atomic-flag-special` -> **miss.**
  > "first, its the only atomic type which is guaranteed to have a lock free implementation ... The other thing that makes it unique is that it does not have a non-modifying read"

  The first way is right. The book's second way is still missing, the same gap as the original miss: because it's guaranteed lock-free, you can build a simple lock from it, and so implement every other atomic type on top of it. The missing plain read and `ATOMIC_FLAG_INIT` (a macro, not `INIT_ATOMIC_FLAG`, and not an enum) are interface details. (b) is right: it's guaranteed lock-free, and `false` means the operations use an internal lock.

### Quiz

1. **Hit.**
   - (a) > "iterators might be pointing to positions or objects which another thread might delete ... the authority over that mutex lies external to the container"

     Correct.
   - (b) > "add_or_update just takes this race window away"

     Correct. The book's earlier instance is the DNS cache (Listing 3.13); the stack's combined `pop()` is the same idea.
   - (c) All three ways: a default value, `pair<mapped_type, bool>`, and a smart pointer that's null when absent.
2. **Hit.** (a) and (b) are right: a fixed bucket count, and resizing would break it.
   - (c) The downside the book names is **"you need a good hash function for the key"**. You quoted "only one writer per bucket" as the downside, but that's the book's complaint about a single `shared_mutex`. Your Hash-parameter paragraph does carry the hash-quality point.
   - Nit: N-fold is the opportunity for concurrency, not a guaranteed minimum of N threads; keys can share a bucket.
3. **Hit.** (a)–(d) match the book: no modification in `value_for`, `erase` doesn't throw, `push_back` leaves the list unchanged, and replacing a value is left to the user's type. (d) could add the book's point that a failed assignment doesn't affect the structure as a whole.
4. **Miss**, on (b).
   - (a) Correct.
   - (b) > "there is no alternative"

     The book gives one: pass a **copy** of each item to the user's code instead of a reference, at the cost of copying large items. The guideline is "don't call user-supplied code **while holding a lock**". You wrote "disallowing user-supplied functions", which loses the important part. The "passes a reference to each item" point only appears indirectly.
   - (c) Matches the book. Concretely, the table's predicates only compare keys, take no locks and keep no references.

### Build

```
threadsafe_list: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK
threadsafe_lookup_table: TSAN 0/20 clean (0 race reports) | ASan+UBSan 0/20 clean | 2-TU link OK
```

Every table run aborts at Test 3's "snapshot contains a later key but missed an earlier one". Tests 1, 2 and 4 pass first.

**Library: `threadsafe_list`.** The book's operations are correct, and `remove_if` destroys the node after unlocking it, as specified. `find_first_copy_if` is a good call: the copy is made while the matching node is locked, and your comment states the hazard exactly.

**Library: your additions and the table.** Four contract misses:
- **(a) `get_map()` isn't a snapshot.** It runs `for_each` bucket by bucket with nothing held across buckets, which is the case Test 3 targets. It fails 20/20.
- **(b) `push_front_if_absent` walks the bucket without node locks, so it races with a removal already in the list.** `head.m` stops operations that *haven't started*. A `remove_if` that's already past the head keeps unlinking and deleting nodes while your walk reads their `next` and `data_ptr`.
  - Scratch check: 1 bucket, 200 keys, one thread looping remove-then-re-add while another updates keys near the end. TSAN reported 1–4 races in each of 3/3 runs. The first report is the read of `cur->next` at `threadsafe_list.hpp:58` against `remove_if`'s write.
  - It can also be a use-after-free.
  - The contract asked that every added operation follow the hand-over-hand rule.
- **(c) `remove_mapping` uses `remove_if`.** That walks the whole bucket and deletes every match; the contract says stop at the key's one entry. One consequence: **Test 1 can't fail.**
  - Scratch check: a two-step add (`find_first_if`, then `push_front`) created duplicates for 494–500 of 500 keys across 3 runs. The full driver with that add still printed TEST 1 PASSED 5/5, because one `remove_mapping` deletes every copy.
  - Your Test 1 code is right. It becomes able to fail once (c) is fixed.
- **(d) `head.m` is held for the whole add walk.** Every `value_for`, add or remove that starts in that bucket during the walk waits, which breaks the contract's "no lock covers a whole bucket" line.
  - My Test 4 only checked one direction (a writer behind a slow reader), so no listed test catches this. That's my spec gap, but the contract line was in the spec.
  - Your comment concludes that "compound operations need coarse scope". That isn't forced here. Insert-if-absent can be one step with hand-over-hand locks alone, using the book's point that threads can't pass each other. Think about *where* the new node goes.

**Driver, against the Tests-to-write list.**
- List Test 1: written, and its asserts can fail. ✔
- List Test 2: written, and its assert can fail. ✔
- Table Test 1: written. Its assert can't fail **because of library item (c)**, not because of the driver.
- Table Test 2: written. Scratch: a version of `value_for` that reads through `find_first_if`'s pointer got a TSAN report in `value_for` in 2/2 runs. ✔
- Table Test 3: written, and it catches the current `get_map()` 20/20. ✔
- Table Test 4: written, and its assert can fail (a bucket-wide lock would block for about 150 ms). ✔
- **Self-explanation: blank.**

Nits (no fix item):
- The table uses `value_type` where the interface said `mapped_type`, and an `int` bucket count.
- The table header includes `<atomic>`, `<condition_variable>` and `<thread>` but not `<vector>` or `<functional>`. The list header includes `<queue>`, `<condition_variable>`, `<atomic>` and `<thread>`, none of which it uses.
- `get_bucket` is public.
- The driver runs Test 4 before Test 3.

### Queue updates

```
python3 tools/review.py log 5.1-data-race-definition hit --note "6.3 warm-up: all four conditions listed; (iv) relaxed pair correctly judged no data race BECAUSE both accesses are atomic despite no ordering. First hit. Residue: (i) still justified by seq_cst 'global ordering' instead of 'both atomic'"
python3 tools/review.py log 5.3-sync-needs-value-read miss --note "6.3 warm-up: (2) ordered via release/acquire, (1)/(3) not -- right; but never said what the program then has (data race = UB), described (3) as 'might or might not get 101'; didn't say (1) is sequenced BEFORE the load so even a true read doesn't order it"
python3 tools/review.py log 5.2-atomic-flag-special miss --note "6.3 warm-up: (a) gave 'guaranteed lock-free' + no plain read / ATOMIC_FLAG_INIT; missed again that it's the basis for building a lock and hence every other atomic type. (b) right (guaranteed lock-free; false => internal lock)"
python3 tools/review.py add 6.3 6.3-list-for-each-user-code "..." --kind miss --source "designing-more-complex-lock-based-data-structures-notes.md 6.3.2 Q4(b)"
python3 tools/review.py add 6.3 6.3-hand-over-hand-locking "..." --kind core --source "designing-more-complex-lock-based-data-structures-notes.md 6.3.2 (Listing 6.13 text)"
python3 tools/review.py add 6.3 6.3-get-map-all-buckets "..." --kind core --source "designing-more-complex-lock-based-data-structures-notes.md 6.3.1 (Listing 6.12 text)"
python3 tools/review.py add 6.3 6.3-fixed-buckets-no-lock "..." --kind core --source "designing-more-complex-lock-based-data-structures-notes.md 6.3.1 Q2"
```

The concept texts are in `review-queue.csv`, via `review.py list -v --section 6.3`.

Results:
- `5.1-data-race-definition` is at streak 1 (due 10-20).
- The other two warm-up items are back to streak 0 (due 10-17).
- The new miss item is due 10-16 and the three core items 10-17.
- 6.3 closes with these items and is `done` in the ledger. The build's open fixes are F14.

### Fix list

- **Opened F14** (`threadsafe_list` + `threadsafe_lookup_table`):
  - (a) `get_map()` must capture one moment.
  - (b) `push_front_if_absent` must lock nodes hand-over-hand.
  - (c) `remove_mapping` stops at the key's one entry.
  - (d) An add must not hold `head.m` for its whole walk.
  - (e) The self-explanation.

  Re-verify: `verify.sh threadsafe_lookup_table`, plus a scratch check that Test 1 fails on a two-step add.

### Beyond the book (ungraded)

None set.
