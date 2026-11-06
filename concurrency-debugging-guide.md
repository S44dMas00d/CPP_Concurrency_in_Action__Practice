# Concurrency Bug Debugging Guide — TSAN, Deadlocks, and the Toolchain

Practical companion to [`concurrency-learning-framework.md`](concurrency-learning-framework.md).
That file's section loop says, at step 3, every build task gets a real concurrent
driver run under `-fsanitize=thread` "before review," and step 4 asks you to report
"what TSAN caught, what you had to fix." This file is how: what TSAN actually does,
how to read what it prints, and the other facilities available once you reach
ch11 ("Types of concurrency-related bugs" / "Techniques for locating
concurrency-related bugs" in the ledger — this doc is a practical head start on
11.2, not a replacement for reading it).

For *how the sanitizer mechanism works under the hood* (instrumentation, shadow
memory, why it needs both a compile and a link flag) see §1.8 of
[`build-toolchain-guide.md`](build-toolchain-guide.md) — not repeated here. This
file is about *using* it.

---

# PART 1 — TSAN in this repo

## 1.1 It's already on

The root [`CMakeLists.txt`](CMakeLists.txt) turns TSAN on by default for every
target:

```cmake
option(ENABLE_TSAN "Build with ThreadSanitizer" ON)
if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
endif()
```

So `cmake --build build` already produces instrumented binaries — there is no
separate "TSAN build" to remember to opt into. Every driver you write for a build
task is a TSAN driver by default.

**Turning it off** (for a plain timing test, or to confirm a bug isn't a sanitizer
artifact): use the `cmake build (no TSAN)` task in `.vscode/tasks.json`, or by hand:

```bash
cmake -S . -B build -G Ninja -DENABLE_TSAN=OFF
cmake --build build -j
```

Remember `CMakeCache.txt` is sticky (§1.4 of the toolchain guide) — flipping the
option back requires either passing `-DENABLE_TSAN=ON` again or deleting `build/`.

## 1.2 Just run the binary

No flags needed at the call site:

```bash
build/ch02_managing_threads/thread_guard/thread_guard_driver
```

If TSAN finds nothing, the program runs and exits exactly as an unsanitized build
would — silence is the "clean" result, there's no separate "no bugs found" banner.
If it finds something, a `WARNING:` or `FATAL:` block prints to **stderr**, and
by default the process aborts after the first report (see `halt_on_error` in
§2.4 to change that).

## 1.3 One caveat: races are probabilistic

TSAN doesn't prove the absence of a race — it can only report races that actually
happen to occur, on the interleaving that actually occurred, during the run you
made. A clean run is evidence, not proof. Two things help:

- **TSAN perturbs scheduling** deliberately (it inserts small delays around
  synchronization) specifically to surface interleavings that would rarely happen
  on a plain run — it's substantially better than "just run it and hope," but still
  not exhaustive.
- **Run it more than once**, and under real concurrent load — the framework's own
  language for this is "multi-producer/consumer or equivalent load," not a
  single-shot smoke test. A driver that only ever has one thread touch shared state
  won't exercise the race at all, clean or not.

```bash
for i in $(seq 1 20); do build/.../my_driver || { echo "FAILED on run $i"; break; }; done
```

---

# PART 2 — Reading TSAN's output

## 2.1 A data race, annotated

This is real output from a minimal repro (two threads incrementing a global
`int counter` with no synchronization) — not a fabricated example:

```
==================
WARNING: ThreadSanitizer: data race (pid=249382)
  Read of size 4 at 0x55555555801c by thread T2:
    #0 increment() race_demo.cpp:3 (race_demo+0x1343)
    #1 ... std::thread machinery ...

  Previous write of size 4 at 0x55555555801c by thread T1:
    #0 increment() race_demo.cpp:3 (race_demo+0x1367)
    #1 ... std::thread machinery ...

  Location is global 'counter' of size 4 at 0x55555555801c (race_demo+0x0000000401c)

  Thread T2 (tid=249386, running) created by main thread at:
    #0 pthread_create ...
    #2 main race_demo.cpp:6

  Thread T1 (tid=249385, finished) created by main thread at:
    #0 pthread_create ...
    #2 main race_demo.cpp:5

SUMMARY: ThreadSanitizer: data race race_demo.cpp:3 in increment()
==================
```

Reading order that gets you to the bug fastest:

1. **`SUMMARY` line, bottom.** File:line and function — jump here first.
2. **The two access blocks** ("Read of size N... by thread T2" / "Previous write...
   by thread T1"). This is the whole point of the report: **two accesses, at least
   one a write, to the same memory, from different threads, with no
   happens-before edge between them.** Same source line here (`counter++` isn't
   atomic — it's a read, an add, and a write) is common; they don't have to match.
3. **`Location is ...`** — which variable/object, and whether it's global, heap,
   or stack. A stack location plus a "created by" thread block below often means a
   variable captured by reference into a lambda that outlived its scope.
4. **The two "Thread TN created by" blocks** — where each racing thread was
   spawned, useful once the source line alone doesn't make the ownership obvious.

**What it does *not* tell you**: which access is "the bug." Both are real; the bug
is the *missing synchronization between them*, not one bad line. The fix is a
mutex, an atomic, or a happens-before edge (e.g. a condition variable) around
*both* accesses — not just the write.

## 2.2 A deadlock (lock-order inversion), annotated

TSAN detects a large and important class of deadlocks **before they actually
hang** — inconsistent lock ordering across threads — by tracking every order in
which mutexes get acquired together and flagging a cycle. Real output, from two
threads that each lock two mutexes in opposite order across many iterations:

```
==================
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=252842)
  Cycle in lock order graph: M9 (0x555555558080) => M10 (0x555555558040) => M9

  Mutex M10 acquired here while holding mutex M9 in thread T1:
    #0 pthread_mutex_lock ...
    #4 thread_a() deadlock_demo.cpp:7
    ...

    Hint: use TSAN_OPTIONS=second_deadlock_stack=1 to get more informative warning message

  Mutex M9 acquired here while holding mutex M10 in thread T2:
    #0 pthread_mutex_lock ...
    #4 thread_b() deadlock_demo.cpp:13
    ...

  Thread T1 (tid=252844, running) created by main thread at: ...
  Thread T2 (tid=252845, running) created by main thread at: ...

SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) ...
==================
```

Reading it:

- **`Cycle in lock order graph: M9 => M10 => M9`** — TSAN names mutexes `M<n>` in
  the order it first saw them (not by variable name — cross-reference with the
  acquisition sites below to know `M9`/`M10` are your `m1`/`m2`).
- **The two "acquired here while holding" blocks** are the two halves of the cycle:
  thread T1 takes `M9` then `M10` (i.e. `m1` then `m2`); thread T2 takes `M10` then
  `M9` (`m2` then `m1`). That mismatch *is* the bug — any two threads that can hit
  this pattern concurrently can deadlock, even if this particular run didn't.
- **The `Hint:`** is worth taking — rerun with `TSAN_OPTIONS=second_deadlock_stack=1`
  (§2.4) to get the *lock* site for each mutex as well as the *acquire-while-holding*
  site, which is often the more useful of the two for tracking down where the first
  lock actually started.

**Important behavioral note, confirmed on this machine**: TSAN's deadlock detector
is a *static-pattern* detector — it reports the inversion as soon as it sees the
cycle, whether or not this particular run actually deadlocks. But if this run
*does* go on to deadlock for real (two threads truly stuck waiting on each other),
**TSAN's report does not rescue the process** — it printed the warning above and
then the process still hung until I killed it with a timeout. TSAN tells you about
the danger; it doesn't prevent it. Always run a deadlock-prone driver under a
timeout (`timeout 10 ./driver`, or the harness equivalent), never bare — a real
deadlock otherwise hangs your terminal (or CI) indefinitely.

## 2.3 The fix pattern for a lock-order inversion

Once you have the report: pick one global order for every pair of mutexes a
piece of code might hold together, and make every thread acquire them in that
order — or use `std::lock(m1, m2)` / `std::scoped_lock(m1, m2)`, which internally
guarantees deadlock-free acquisition regardless of argument order. (This is
exactly the "lazy init," "lock ordering," `std::lock` material the framework's
ledger flags for 3.2 — this report is what that section's failure mode looks like
under a real sanitizer instead of on paper.)

## 2.4 `TSAN_OPTIONS` reference

Set as an environment variable, colon-separated, e.g.:

```bash
TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1" build/.../my_driver
```

| Option | Effect |
|---|---|
| `halt_on_error=0` | Keep running after the first report instead of aborting — see *every* race/deadlock in one run rather than fixing them one-by-one across many runs. Almost always what you want while iterating on a build task. |
| `second_deadlock_stack=1` | Also show *where each mutex was locked*, not just where it was acquired-while-holding-another. See §2.2. |
| `exitcode=N` | Process exit code when TSAN reports something (default `66`) — useful if you're scripting a pass/fail check. |
| `history_size=N` (0-7) | How much per-thread access history TSAN keeps, trading memory for the ability to report older races. Raise it if a race's "previous write" stack looks truncated/missing on a long-running driver; default is usually enough for short exercise drivers. |
| `suppressions=path/to/file` | Point at a suppressions file (§2.5). |

Print every available option and its current value:

```bash
TSAN_OPTIONS=help=1 build/.../my_driver
```

Full list: <https://github.com/google/sanitizers/wiki/ThreadSanitizerFlags>

## 2.5 Suppressing known/third-party noise

If TSAN ever flags something in system or library code you don't control (rare for
this repo's scope, but it happens with some libc/libstdc++ internals on certain
distros), don't silently ignore the report — suppress it explicitly and by name so
the suppression is visible and specific:

```
# tsan.supp
race:some_specific_function_name
```

```bash
TSAN_OPTIONS="suppressions=tsan.supp" build/.../my_driver
```

Never suppress by broad pattern, and never suppress something inside your own
`.hpp`/`.cpp` — a suppression there is a bug you've hidden, not fixed.

---

# PART 3 — Other facilities

## 3.1 GDB for a real hang

When a build genuinely deadlocks (§2.2's second half) and you want to see *where*
each thread is actually stuck rather than just knowing a cycle exists, attach GDB
instead of killing the process:

```bash
gdb -p $(pgrep my_driver)
```

Useful commands once attached:

| Command | Shows |
|---|---|
| `info threads` | Every live thread and its current stack frame — the fastest way to see who's stuck where |
| `thread N` | Switch focus to thread N |
| `bt` | Backtrace of the current thread |
| `thread apply all bt` | Backtrace of **every** thread at once — usually the first thing to run on a hang |

This is the same GDB wired into `launch.json` (`.vscode/launch.json`) for F5 —
`handle SIGCHLD nostop noprint pass` there exists specifically so stepping through
a multithreaded program doesn't stop on every thread's lifecycle signal, per
`build-toolchain-guide.md` §1.7.

## 3.2 ASan and UBSan — different bug classes

TSAN catches races and this class of deadlock. It does **not** catch memory
corruption or undefined behavior in general — that's what the other sanitizers are
for, on the same `-fsanitize=` mechanism:

```bash
g++ -fsanitize=address -g -O1 file.cpp -o out      # AddressSanitizer: heap/stack
                                                     # overflows, use-after-free,
                                                     # double-free
g++ -fsanitize=undefined -g -O1 file.cpp -o out     # UBSan: signed overflow, null
                                                     # deref, bad casts, etc.
```

**ASan and TSAN cannot be combined** — they both instrument memory access in
incompatible ways and claim overlapping address space (§1.8 of the toolchain
guide). If a build task's driver needs both kinds of checking, run it twice: once
with `ENABLE_TSAN`, once with a separate `-fsanitize=address` build. UBSan *can*
be combined with either.

## 3.3 Valgrind / Helgrind — not installed here

Helgrind (part of Valgrind) is another race/deadlock detector, older than TSAN and
running via full instruction emulation rather than compile-time instrumentation.
It's **not installed on this machine** (`valgrind` isn't on PATH) and isn't needed
day to day — TSAN is faster, actively maintained, and already wired into every
build here. Worth knowing it exists mainly because some codebases and CI setups
you'll meet elsewhere use it instead:

```bash
sudo apt install valgrind
valgrind --tool=helgrind ./my_driver     # no recompile needed — this is the
                                          # actual advantage over TSAN, which needs
                                          # -fsanitize=thread baked into the build
valgrind --tool=drd ./my_driver          # DRD: an alternative race detector,
                                          # different heuristics than Helgrind
```

Tradeoff: no rebuild required, but 10-50x slowdown (vs. TSAN's 5-15x) and no
build-time integration — you'd run it by hand, not via `ENABLE_TSAN`.

---

# PART 4 — Historical note: the `vm.mmap_rnd_bits` change

This section is a durable record of a machine-level change made while setting up
this repo, kept here specifically so it can be found and reverted later if needed.

## 4.1 What was changed

```bash
sudo sysctl -w vm.mmap_rnd_bits=28
echo 'vm.mmap_rnd_bits=28' | sudo tee /etc/sysctl.d/99-tsan.conf
```

This lowers the kernel's ASLR (address-space-layout-randomization) entropy for
`mmap`-placed memory, system-wide, on this machine, persistently across reboots
(the second command). It is **not** scoped to this repo, this user, or C++
programs — it affects every process on the machine, permanently, until reverted.

## 4.2 Why it was made

Ubuntu's `6.8` kernel series (this machine runs `6.8.0-138-generic`) raised the
default from `28` to `32`. TSAN (and ASan) reserve fixed regions of the address
space for their shadow memory and require the program and its libraries to load
inside specific ranges (detailed in `build-toolchain-guide.md` §1.8). At
`mmap_rnd_bits=32`, the randomization span is wide enough (~16 TB) to regularly
place a PIE binary or its shared libraries outside those ranges, so a sanitized
binary run **outside a debugger** aborted before `main()`:

```
FATAL: ThreadSanitizer: unexpected memory mapping 0x62f62decc000-0x62f62dece000
```

(GDB was never affected — it disables ASLR for the process it launches by
default, so F5 debugging worked throughout, before and after this change.)

We confirmed `28` is exactly enough entropy to keep a PIE binary inside TSAN's
window (~1 TB span vs. TSAN's ~1.5 TB reserved range) while `32` regularly isn't —
verified by running the failing binary repeatedly and observing that its load
address consistently exceeded TSAN's ceiling under the default setting, and never
did after the change. `28` was also Ubuntu's own long-standing default before the
`6.8` kernel raised it — not an unusually low or exotic value.

We considered and rejected a per-run alternative first (wrapping every binary in
`setarch -R`, wired into CMake) — see the "Repo-local alternative" discussion in
the earlier design of this repo. It was reverted in favor of this system-wide fix
once it was clear the wrapper only protected this repo and would need
reintroducing in every future sanitized project, whereas the sysctl fixes it
everywhere, once.

## 4.3 Impact — does it cost performance?

**No measurable runtime cost.** `vm.mmap_rnd_bits` only affects the one-time
random base address chosen at `execve()`; it changes nothing about how fast
compiled code runs, memory usage, cache behavior, or I/O. Drawing a 28-bit random
number costs the same as drawing a 32-bit one.

**What it does cost: ASLR entropy**, system-wide:

| | Before | After |
|---|---|---|
| `vm.mmap_rnd_bits` | 32 | 28 |
| Possible load addresses | ~4.3 billion | ~268 million (16x fewer) |

ASLR stays **on** — this reduces the search space an attacker would have to brute
force, it does not disable randomization. The realistic exposure is a network-facing,
crash-tolerant service being remotely brute-forced for an exploitable memory bug;
irrelevant to a local dev machine running study exercises, and `28` was the
standard value for roughly a decade before Ubuntu changed it.

## 4.4 How to check whether it's currently applied

```bash
sudo sysctl vm.mmap_rnd_bits            # should print 28
cat /etc/sysctl.d/99-tsan.conf          # should exist, containing vm.mmap_rnd_bits=28
```

(Reading `vm.mmap_rnd_bits` without `sudo` returns "permission denied" — that's
normal kernel hardening, not a sign anything is wrong.)

## 4.5 How to roll it back

Removes the persistent file and restores the pre-change value for future boots;
`sysctl -w` additionally restores it immediately without a reboot:

```bash
sudo rm /etc/sysctl.d/99-tsan.conf
sudo sysctl -w vm.mmap_rnd_bits=32
```

After rolling back, sanitized binaries will again need either a debugger (F5,
unaffected either way) or `setarch -R ./binary` to run standalone from a terminal
— the original symptom in §4.2 returns by design, since the fix is being removed.

## 4.6 If this machine is shared

This change affects every user and process on the machine, not just this repo. If
this machine is shared with other people or other projects, confirm before
reverting or reapplying that nothing else is depending on the current state —
the same `28` also benefits any other sanitizer use on the box, so reverting only
makes sense if the goal is specifically to restore Ubuntu's `6.8` default.
