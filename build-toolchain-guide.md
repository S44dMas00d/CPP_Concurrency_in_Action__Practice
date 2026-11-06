# Build, Lint & Debug Toolchain — Explainer + Setup Guide

Everything that had to be true for three things to work in this repo: **format on
save**, **CMake build**, and **F5 debugging**. Part 1 explains each component and
why it exists. Part 2 is the procedure to reproduce the setup from scratch.

Machine snapshot this was written against:

| Component | Version | Path |
|---|---|---|
| CMake | 3.22.1 | `/usr/bin/cmake` |
| Ninja | 1.10.1 | `/usr/bin/ninja` |
| GNU Make | 4.3 | `/usr/bin/make` |
| GCC / g++ | 11.4.0 | `/usr/bin/gcc`, `/usr/bin/g++` |
| libstdc++ headers | 11 | `/usr/include/c++/11` |
| GDB | 12.1 | `/usr/bin/gdb` |
| clang-format | 14.0.0 | `/usr/bin/clang-format` |
| clang-format (bundled in cpptools) | 22.1.3 | inside the extension |
| C/C++ extension (cpptools) | 1.33.8 | `~/.vscode/extensions/` |
| CMake Tools extension | 1.23.52 | `~/.vscode/extensions/` |
| ARM cross-compilers (unrelated, but a hazard) | 9.3.1 / 10.3.1 | `/opt/...`, `/usr/bin/` |

---

# PART 1 — EXPLAINERS

## 1.1 The layer cake

Nothing here is one program doing everything. Each layer only knows about its
immediate neighbours:

```
  your source (.cpp / .hpp)
        |
  [ CMake ]                  reads CMakeLists.txt, decides WHAT to build
        |  generates
  [ Ninja or Make ]          decides WHEN to rebuild, runs the commands
        |  invokes
  [ g++ ]                    compiler driver: preprocess -> compile -> assemble -> link
        |  produces
  an executable  ----------> [ GDB ] debugs it
        |                    [ TSAN runtime ] instruments it at run time
        |
  side artifact: compile_commands.json  --> [ cpptools IntelliSense ]
  side artifact: .cmake/api codemodel   --> [ CMake Tools target pickers ]
```

The single most useful idea: **CMake does not build anything, and Ninja does not
compile anything.** CMake writes build files; Ninja reads them and runs `g++`;
`g++` is itself a driver that runs several other programs. Most confusing errors
come from mixing up which layer is complaining.

## 1.2 From source code to executable — the five stages

Before any tool names, the process. C++ has a specific, old, and rigid pipeline.
Every tool in this document is either *performing* one of these stages, *ordering*
them, or *reading the leftovers* they produce.

```
  main.cpp   thread_guard.hpp
      |            |
      +-----+------+
            |
   (1) PREPROCESSOR  cpp        #include, #define, #ifdef  -> pure C++ text
            |                   output: one "translation unit"
   (2) COMPILER      cc1plus    parse, type-check, optimize -> assembly text
            |                   output: main.s
   (3) ASSEMBLER     as         assembly text -> machine code
            |                   output: main.o   (an OBJECT FILE)
            |
   (4) LINKER        ld         combine .o files + libraries, resolve symbols
            |                   output: an EXECUTABLE
            |
   (5) LOADER        ld.so      at run time: map into memory, bind shared libs
            |
        running process
```

Stages 1-3 run **once per `.cpp` file, independently**. Stage 4 runs **once for the
whole program**. That asymmetry explains most of C++'s structure and most of its
errors.

### The stages in detail

**(1) Preprocessor.** A text substitution engine that knows nothing about C++. It
pastes `#include`d files in literally, expands macros, and strips `#if`-disabled
branches. The result — your `.cpp` plus every header it pulled in, transitively —
is a **translation unit**, often hundreds of thousands of lines. This is why
`#include <iostream>` is not free, and why headers need include guards
(`#pragma once` in this repo) to avoid being pasted twice.

**(2) Compiler (proper).** Takes one translation unit and produces assembly for one
target architecture. This is where syntax errors, type errors, template
instantiation, and optimization live. **It sees exactly one translation unit and
nothing else** — it cannot know whether a function you merely *declared* actually
exists somewhere. That check is deferred to the linker.

**(3) Assembler.** Mechanically converts assembly mnemonics into binary machine
code, emitting an **object file** (`.o`). An object file is not runnable. It
contains machine code with holes, plus two lists:

- **defined symbols** — "I have `thread_guard::~thread_guard` at offset 0x40"
- **undefined symbols** — "I call `std::thread::join`; someone please supply it"

**(4) Linker.** Collects every `.o` and library, matches each undefined symbol to
exactly one definition, patches the holes with real addresses, and lays out the
final binary. Its errors are therefore a completely different species from
compiler errors:

| Error | Stage | Means |
|---|---|---|
| `expected ';' before '}'` | compile | syntax, in one file |
| `no matching function for call to ...` | compile | types, in one file |
| `undefined reference to 'foo'` | **link** | declared and used, never defined — or you forgot to link the library |
| `multiple definition of 'foo'` | **link** | defined in two translation units |

**(5) Loader.** Not part of the build. At execution, the kernel and the dynamic
loader (`ld.so`) map the binary into memory and bind shared libraries. **This is
the stage where the TSAN "unexpected memory mapping" failure occurs** — the build
succeeded completely; the program dies while being placed in memory.

### Why headers exist, and what belongs in them

Because stage 2 sees one translation unit at a time, calling a function defined in
another `.cpp` requires *telling* the compiler its signature. That promise is a
**declaration**; headers are the mechanism for sharing declarations.

```cpp
// declaration — "this exists somewhere". Enough for the COMPILER.
void join_all(std::vector<std::thread>& ts);

// definition — the actual body. Needed by the LINKER.
void join_all(std::vector<std::thread>& ts) { for (auto& t : ts) t.join(); }
```

The **One Definition Rule**: exactly one definition per program. Put an ordinary
function *definition* in a header, include it from two `.cpp` files, and you get
`multiple definition` at link time.

**Templates are the exception, and it's why this repo is header-only.** A template
isn't code until it's instantiated with concrete types, and instantiation happens
during stage 2 — so the compiler needs the *full body* available in every
translation unit that uses it. It cannot be hidden in a separate `.cpp`. Hence
`threadsafe_queue<T>` lives entirely in `.hpp`, hence the CMake target is an
`INTERFACE` library (§1.4) with nothing to compile. `inline` functions and
`constexpr` are header-friendly for the same reason: they're explicitly exempted
from the ODR's one-definition-per-program constraint.

### Libraries: two ways to reuse code

A library is just a bundle of object files.

- **Static** (`.a`) — an archive of `.o` files. The linker copies the parts you use
  *into* your executable at stage 4. Bigger binary, no runtime dependency.
- **Shared / dynamic** (`.so`) — kept separate. The linker only records "I need
  `libstdc++.so.6`"; the *loader* resolves it at stage 5. Smaller binary, updatable
  independently, but must be present at run time.

`ldd <binary>` lists the shared libraries stage 5 will load — which is exactly how
we confirmed `libtsan.so.0` was really linked into your drivers.

- **Header-only** — the third option, not really a library at all: no `.o`, no
  archive, just declarations and definitions the compiler re-processes in every
  translation unit. Both components in this repo are this kind.

### `g++` is a conductor, not a compiler

Critically, **you never invoke stages 1-4 yourself.** `g++` is a *driver* that runs
them in order and passes the right flags along. One command hides four programs:

```bash
g++ -c main.cpp -o main.o    # stages 1,2,3 — "compile only", stop before linking
g++ main.o -o my_program     # stage 4 only — link
g++ main.cpp -o my_program   # all four
```

That `-c` split is the single most useful thing to know about the pipeline, because
it's how build systems work: compile each `.cpp` to a `.o` separately (parallel, and
re-doable for just the file you changed), then link once. **This is exactly what
Ninja is scheduling and what incremental builds exploit** — edit one `.cpp`, and
only its `.o` is rebuilt before relinking.

You can watch the stages individually:

```bash
g++ -E main.cpp | wc -l      # (1) preprocessed size (a bare <iostream> -> ~32,000 lines)
g++ -S main.cpp              # (2) stop after compiling -> main.s
g++ -c main.cpp              # (3) stop after assembling -> main.o
nm -C main.o                 # inspect symbols: 'T' = defined, 'U' = undefined
g++ -v main.cpp              # print every sub-program the driver actually runs
```

### Where each flag acts

Flags are not interchangeable across stages, which is the root of several classic
mistakes:

| Flag | Stage | Effect |
|---|---|---|
| `-I<dir>` | 1 | where to search for `#include`d headers |
| `-D<name>` | 1 | define a macro |
| `-std=c++17` | 2 | language standard |
| `-Wall -Wextra` | 2 | warnings |
| `-O1 / -O2` | 2 | optimization level |
| `-g` | 2 | emit DWARF debug info (what GDB reads) |
| `-c` | 1-3 | stop before linking |
| `-l<name>` / `-L<dir>` | 4 | link this library / search here for it |
| `-fsanitize=thread` | **2 and 4** | instrument code *and* link the runtime |

That last row is the one that bites people, and it's why the root `CMakeLists.txt`
passes `-fsanitize=thread` through **both** `add_compile_options` and
`add_link_options`. Give it to only the compiler and you get `undefined reference`
errors from the linker; give it to only the linker and nothing is instrumented.

### Where the tools sit on this pipeline

| Tool | Relationship to the five stages |
|---|---|
| **CMake** | Above all of them. Decides *what* to build and writes the recipe. Runs no stage itself. |
| **Ninja / Make** | Schedules the recipe: which `.cpp` needs stages 1-3 again, and when to run stage 4. |
| **`g++`** | The driver that actually performs stages 1-4 by invoking `cc1plus`, `as`, `ld`. |
| **`clang-format`** | Off to the side. Parses your source and rewrites its whitespace. Never compiles, never links, needs no build. |
| **cpptools IntelliSense** | Simulates stages 1-2 in the editor for diagnostics and completion; uses `compile_commands.json` to copy the real flags. |
| **GDB** | Operates on the stage-4 output, reading the DWARF that `-g` embedded at stage 2. |
| **TSAN** | Both sides: instrumentation added at stage 2, runtime library linked at stage 4, and the checks run in the live process after stage 5. |
| **`ld.so` (loader)** | Stage 5, at every program start. |

A useful diagnostic habit follows directly: **name the stage before guessing.**
A missing header is stage 1, a type error stage 2, `undefined reference` stage 4,
and a crash before `main()` — like the TSAN mapping failure — is stage 5.

## 1.3 GNU, GCC, gcc, g++ — and what a "toolchain" is

- **GNU** is the umbrella free-software project. Most of the userland here is GNU:
  the compiler, the debugger, `make`, the C library.
- **GCC** means *GNU Compiler Collection* — the whole project, covering C, C++,
  Fortran, and more.
- **`gcc`** and **`g++`** are two *driver* programs into that collection. A driver
  doesn't compile; it decides which real programs to run and in what order:
  `cc1plus` (the actual C++ compiler), `as` (assembler), `collect2`/`ld` (linker).
- The practical difference: **`g++` links the C++ standard library
  (`libstdc++`) automatically and treats `.c` files as C++.** `gcc` does not link
  `libstdc++` unless you pass `-lstdc++`. For a C++ project you always want `g++`.
  This is why `CMakeLists.txt` sets `CMAKE_CXX_COMPILER`, not just `CMAKE_C_COMPILER`.

A **toolchain** is the full set that turns source into a running program:

```
  compiler (cc1plus) + assembler (as) + linker (ld)
  + C library (glibc) + C++ library (libstdc++) + runtime startup (crt*.o)
```

**Hosted vs freestanding — why the ARM compilers are dangerous here.**
`/usr/bin/g++` is a *native, hosted* toolchain: it builds programs for this same
x86-64 Linux machine and assumes a full OS underneath (files, threads, `exit()`).
`arm-none-eabi-g++` is a *cross, freestanding* toolchain: it builds ARM machine
code for a bare-metal microcontroller with **no OS, no pthreads, no hosted libc**.
The `none` in the triple literally means "no operating system".

If CMake caches an ARM compiler for this project, configuration may even appear to
succeed and then linking fails in confusing ways — and every `std::thread` in this
repo is unsupported, because there is no threading library to link against. That is
the entire reason `CMakeLists.txt` pins `/usr/bin/g++` and why kit choice matters.

## 1.4 CMake

**CMake is a build-system generator, not a build system.** You describe intent in
`CMakeLists.txt`; CMake emits native build files for whatever build tool you chose.

Three distinct stages, which the error messages distinguish and people often don't:

1. **Configure** — read `CMakeLists.txt`, probe the compiler ("does it work? does it
   support C++17?"), resolve variables. Results are cached.
2. **Generate** — write `build.ninja` (or `Makefile`) from the configured state.
3. **Build** — hand off to Ninja/Make, which run the compiler.

`cmake -S . -B build` does 1 and 2. `cmake --build build` does 3.

### Out-of-source builds
`-S .` is the source dir, `-B build` the build dir. Every generated artifact lands
in `build/`, so the source tree stays clean and `build/` is disposable — which is
why `build/` is in `.gitignore` and why "delete `build/` and reconfigure" is a safe,
routine repair.

### CMakeCache.txt — the sticky bit
Configure results are written to `build/CMakeCache.txt` and **persist across runs**.
This is a deliberate feature (fast reconfigures, remembered options) and the source
of most "why won't it pick up my change" confusion. Notably the **compiler and the
generator are locked into the cache on first configure and cannot be changed
in place.** Changing them requires deleting `build/`.

### Generators
The generator is the choice of *which build tool's files to emit*:

- `-G "Unix Makefiles"` -> writes `Makefile`, built by `make`
- `-G Ninja` -> writes `build.ninja`, built by `ninja`

A build dir belongs to exactly one generator. Pointing a second generator at the
same directory fails with *"generator ... does not match the generator used
previously"*. **CMake Tools defaults to Ninja; a bare `cmake -S . -B build` on this
machine defaults to Unix Makefiles.** If a shell task and the extension disagree,
they will fight over the same `build/`. That is why both tasks in `tasks.json`
now pass `-G Ninja` explicitly.

### Targets
A target is a thing to build, plus the properties that describe how to use it.

- `add_executable(name src.cpp)` — produces a runnable binary.
- `add_library(name INTERFACE)` — produces **no compiled output at all**. An
  INTERFACE library is pure metadata: "anyone linking me gains these include
  directories / flags". It is the correct model for a **header-only** library,
  which is what `thread_guard` and `threadsafe_queue` are — templates and inline
  code live entirely in `.hpp`, so there is nothing to compile until a consumer
  includes them.
- `target_link_libraries(driver PRIVATE thread_guard)` — propagates that metadata.
  With INTERFACE libraries this is how the include path reaches the driver; no
  actual linking happens.
- `add_subdirectory(path)` — pulls in a nested `CMakeLists.txt`, which is how one
  root file composes many small per-exercise files.

By default CMake mirrors the source tree inside `build/`, so
`ch02_managing_threads/thread_guard/tests/thread_guard_driver.cpp`
becomes `build/ch02_managing_threads/thread_guard/thread_guard_driver`.
(You can override this per target with `RUNTIME_OUTPUT_DIRECTORY` — the sibling
DSA project does exactly that to put each binary next to its own source file.)

### Two side artifacts that editors depend on
These are the reason the editor feels smart, and the reason it silently doesn't:

1. **`compile_commands.json`** — enabled by `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`.
   A JSON array of the exact compiler invocation for every source file: flags,
   include paths, standard. IntelliSense reads this instead of guessing, so its
   view matches the real build exactly. It is a widely supported format, not a
   CMake- or VS Code-specific one.
2. **The CMake File API** — a request/reply protocol living in
   `build/.cmake/api/v1/`. A client drops an empty marker file in `query/`; on the
   next configure, CMake writes a JSON **codemodel** into `reply/` describing every
   target, its type, and its artifact paths. **CMake Tools populates its build and
   debug target pickers exclusively from this codemodel.** Its query marker is
   named `client-vscode`.

   The consequence is unintuitive but important: **a build directory created by a
   plain shell `cmake` command contains no File API query, so CMake Tools sees no
   targets — even though the project builds perfectly from the terminal.** CMake
   Tools must configure at least once itself. This is exactly why
   `"cmake.configureOnOpen": true` is required rather than optional here.

## 1.5 Make and Ninja

Both solve the same problem: given a dependency graph and file timestamps, run the
minimum set of commands needed to bring outputs up to date.

- **GNU Make** (1976, still everywhere) is general-purpose and meant to be written
  by humans, with its own macro language. That generality makes it slower at
  large-graph up-to-date checks.
- **Ninja** is deliberately *not* human-friendly. It has almost no logic, because it
  assumes a generator writes its input. In exchange it is very fast at deciding
  what to rebuild, which is why it's the modern default.

For a project this size the speed difference is irrelevant; **consistency matters,
correctness of choice doesn't.** Pick one and make every entry point use it.

## 1.6 Clang, LLVM, and clang-format

**LLVM** is a compiler infrastructure project, entirely separate from GNU/GCC.
**Clang** is its C/C++ front end — a full alternative to `g++`.

**`clang-format` is a standalone formatter built on Clang's parser. It does not
compile your code, is not tied to using Clang as your compiler, and needs no build
system.** We compile with GCC and format with Clang's tool; that is normal.

- **`.clang-format`** at the repo root is a YAML file of style rules. The setting
  `"C_Cpp.clang_format_style": "file"` means *walk up the directory tree from the
  file being formatted until a `.clang-format` is found* — so one root file governs
  the whole repo.
- **Version sensitivity is real.** Options get added, renamed, and removed between
  releases. This repo's `.clang-format` was generated by clang-format 14 and still
  contains keys that later versions dropped (`AllowAllConstructorInitializersOnNextLine`,
  `ConstructorInitializerAllOnOneLineOrOnePerLine`, `IndentRequires`). Newer builds
  ignore them rather than erroring, so formatting silently *drifts* instead of
  failing loudly. cpptools ships its own clang-format (22.1.3) which would be used
  by default — hence pinning `C_Cpp.clang_format_path` to `/usr/bin/clang-format`
  (14.0.0) so the editor and the command line produce byte-identical output.
- Worth knowing about this particular config: **`ColumnLimit: 0` disables all line
  wrapping.** clang-format will fix indentation, brace placement and spacing but
  will never break or join your long lines. If formatting looks like it "did
  nothing" to a long line, this is why — it's intended.
- Useful invocations:
  - `clang-format --dry-run file.hpp` — report violations, change nothing.
  - `clang-format -i file.hpp` — rewrite in place.
  - `clang-format --dump-config` — print the fully resolved style, a quick way to
    confirm your file parses.

## 1.7 GDB and the debug adapter chain

**GDB** is the GNU debugger: breakpoints, stepping, inspecting memory and threads.
Debug info comes from compiling with **`-g`**, which embeds DWARF records mapping
machine addresses back to source lines and variable names. No `-g`, no useful
debugging. (`CMAKE_BUILD_TYPE=Debug` adds `-g` for you; this repo's TSAN block
passes `-g` explicitly.)

VS Code does not speak GDB. The chain is:

```
VS Code  --Debug Adapter Protocol-->  cppdbg (in cpptools)  --GDB/MI-->  gdb
```

**GDB/MI** is a line-based machine interface mode built into GDB for exactly this.
Entries in `setupCommands` are raw MI commands sent at startup — which is why they
look nothing like JSON settings:

- `-enable-pretty-printing` turns on Python pretty-printers, so a `std::vector`
  displays as its elements rather than raw internal pointers. Essential for C++.
- `handle SIGCHLD nostop noprint pass` stops GDB halting on routine thread
  lifecycle signals — without it, a concurrency program is unusable under the
  debugger.

**A property that matters a lot here: GDB disables ASLR for the process it
launches** (`disable-randomization` is on by default), to make runs reproducible.
That single fact is why the TSAN/ASLR crash in 1.8 does *not* occur under F5 but
does occur from a terminal.

## 1.8 Sanitizers — TSAN specifically

ThreadSanitizer detects data races: unsynchronized concurrent access where at least
one access is a write. It is **two halves that must both be present**:

1. **Compile-time instrumentation** (`-fsanitize=thread` as a *compile* option) —
   the compiler injects a callback around every memory access.
2. **A runtime library** (`-fsanitize=thread` as a *link* option) — pulls in
   `libtsan.so`, which records happens-before relationships and reports races.

Passing the flag to only one half produces link errors or silent non-detection.
The root `CMakeLists.txt` correctly does both:

```cmake
add_compile_options(-fsanitize=thread -g -O1)
add_link_options(-fsanitize=thread)
```

`-O1` is deliberate: unoptimized TSAN builds are punishingly slow, and heavy
optimization damages stack traces. Expect roughly **5-15x slowdown and ~5-10x
memory**; this is a debugging build, never a release one.

**Shadow memory and the ASLR conflict.** TSAN maintains "shadow memory" describing
every byte of your program's memory, and to do so cheaply it requires your program
to be mapped into *specific fixed address ranges*. Modern kernels randomize mappings
more aggressively (higher `vm.mmap_rnd_bits`), which can place your program where
TSAN expects its own shadow region. The result, before `main()` even runs:

```
FATAL: ThreadSanitizer: unexpected memory mapping 0x63c39c78a000-0x63c39c78c000
```

This is an environment incompatibility, not a bug in your code. Fixes:

- Permanent: `sudo sysctl -w vm.mmap_rnd_bits=28` (persist in `/etc/sysctl.d/`).
- Ad hoc, no root: `setarch -R ./your_binary` disables ASLR for one run.
- Under F5 it never appears, because GDB already disables randomization.

**`TSAN_OPTIONS`** is a colon-separated runtime environment variable:
- `halt_on_error=0` — report every race and keep running, instead of dying on the
  first. Almost always what you want while learning.
- `second_deadlock_stack=1` — show both stacks involved in a lock-order inversion.

Related sanitizers you'd use the same way, one at a time: **ASan** (`address`,
buffer overruns/use-after-free) and **UBSan** (`undefined`). **ASan and TSAN are
mutually exclusive** — they both claim the address space.

## 1.9 The VS Code side

### Extensions and their division of labour
- **cpptools** (`ms-vscode.cpptools`) — IntelliSense, and it also *proxies*
  formatting (it invokes a clang-format binary; it is not itself a formatter) and
  ships the **`cppdbg`** debug adapter.
- **CMake Tools** (`ms-vscode.cmake-tools`) — drives configure/build, owns the
  status-bar kit/variant/target pickers, and exposes commands such as
  `${command:cmake.launchTargetPath}`.

They are independent: **cpptools can give you perfect IntelliSense from
`compile_commands.json` while CMake Tools shows no targets at all**, which is
precisely the state this project was in.

### The three JSON files

They are all workspace-scoped, live in `.vscode/`, and allow `//` comments
(the format is JSONC, not strict JSON).

| File | Answers | Scope |
|---|---|---|
| `settings.json` | "how should the editor and extensions behave here?" | overrides your user settings for this folder only |
| `tasks.json` | "what shell commands can I run?" | Ctrl+Shift+B, and `preLaunchTask` |
| `launch.json` | "how do I start this under a debugger?" | F5 |

**`settings.json` precedence**: Default -> User -> **Workspace** -> language-specific
block. A `"[cpp]": { ... }` block beats a plain top-level key for C++ files. This
matters — the user-level settings on this machine already had a `[cpp]` block, and
knowing it wins is how the formatter conflict was diagnosed.

**`tasks.json`**: `"type": "shell"` runs a command line; `"group": {"kind":"build",
"isDefault": true}` makes it the Ctrl+Shift+B task and the one `preLaunchTask`
resolves by label. `"problemMatcher": ["$gcc"]` is a built-in regex that parses
`file:line:col: error: msg` output into clickable Problems-panel entries — without
it, compiler errors are just terminal text.

**`launch.json`**: `"type": "cppdbg"` selects the adapter from 1.7. `program` is the
binary to run, `preLaunchTask` names a `tasks.json` label to run first (this is the
only link between the two files), `environment` sets variables such as
`TSAN_OPTIONS`, and `setupCommands` are raw GDB/MI commands.

**Variable substitution** works across all three:
`${workspaceFolder}` (repo root), `${file}`, `${fileDirname}`,
`${fileBasenameNoExtension}`, `${command:...}` (ask an extension for a value),
`${input:...}` (prompt the user — used for the driver picker here).

### Kits
A **kit** is CMake Tools' record of one toolchain: a name plus compiler paths. It
scans your PATH and well-known locations and caches results in
`cmake-tools-kits.json`. Selecting a kit makes CMake Tools pass
`-DCMAKE_C_COMPILER=... -DCMAKE_CXX_COMPILER=...` on the configure command line.

**Critical interaction:** because a kit passes those as explicit `-D` arguments, the
defensive guard in `CMakeLists.txt`

```cmake
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER /usr/bin/g++)
endif()
```

**never fires under CMake Tools** — the variable is already defined. That guard
protects the plain shell-task path only. Under CMake Tools, *choosing the right kit
is the only protection*. On this machine three kits are registered and **two are
ARM**, so the wrong pick is a coin flip:

```
GCC 10.3.1 arm-none-eabi        <- wrong (bare metal)
GCC 11.4.0 x86_64-linux-gnu     <- correct
GCC  9.3.1 arm-none-eabi        <- wrong (bare metal)
```

**Where the kits file lives — snap confinement.** This VS Code is installed as a
**snap**, a containerized package format that remaps `$HOME` to a per-revision
sandbox. So the file is *not* at the documented `~/.local/share/CMakeTools/` but at:

```
/home/<user>/snap/code/259/.local/share/CMakeTools/cmake-tools-kits.json
```

where `259` is the snap revision and changes on update. Anything that claims to
live in `~/.config` or `~/.local` for this editor should be looked for under
`~/snap/code/<rev>/` too.

## 1.10 What actually happens when you press F5

1. VS Code reads `launch.json`, picks the configuration.
2. `preLaunchTask: "cmake build"` resolves to that label in `tasks.json` and runs it.
3. The shell runs `cmake -S . -B build -G Ninja` (configure + generate), then
   `cmake --build build -j` (which invokes `ninja`).
4. Ninja compares timestamps and runs `g++` only where needed. `g++` drives
   `cc1plus` -> `as` -> `ld`, injecting TSAN instrumentation and linking `libtsan`.
5. `$gcc` problemMatcher parses any diagnostics into the Problems panel.
6. `program` is resolved. `${command:cmake.launchTargetPath}` asks CMake Tools,
   which answers **from the File API codemodel** — so this step fails outright if
   CMake Tools has never configured.
7. cpptools launches `gdb`, which disables ASLR (side-effect: TSAN is happy).
8. `setupCommands` MI commands run: pretty-printers on, SIGCHLD quietened.
9. `environment` applies `TSAN_OPTIONS`; the program runs; DAP relays breakpoints,
   stack frames and variables back to the UI.

Meanwhile and completely independently: on every save, cpptools shells out to
`/usr/bin/clang-format`, which finds `.clang-format` by walking up from the file.

## 1.11 Every failure we actually hit, and its real cause

| Symptom | Real cause | Fix |
|---|---|---|
| Format-on-save did nothing | `editor.formatOnSave` was never enabled | set it in workspace settings |
| Still nothing after enabling | **two** formatter extensions (cpptools + astyle) claimed C++; VS Code refuses to guess and fails silently | set `editor.defaultFormatter` inside `[cpp]`/`[c]` |
| Editor formatting could drift from CLI | cpptools' bundled clang-format 22 vs system 14, with dropped config keys | pin `C_Cpp.clang_format_path` |
| Target pickers empty | `build/` was made by shell `cmake`, so it had no File API query; CMake Tools had never configured | `"cmake.configureOnOpen": true`, then **CMake: Configure** |
| `generator does not match the generator used previously` | shell `cmake` cached Unix Makefiles; CMake Tools wanted Ninja | delete `build/`, make every entry point pass `-G Ninja` |
| `No usable generator found` + kit selection dead | pinning `"cmake.generator"` restarted the CMake driver into a state it couldn't resolve | **remove** `cmake.generator`; let CMake Tools default to Ninja |
| `FATAL: ThreadSanitizer: unexpected memory mapping` | kernel ASLR entropy vs TSAN's fixed shadow layout | `sudo sysctl -w vm.mmap_rnd_bits=28`, or `setarch -R` |
| Couldn't find `cmake-tools-kits.json` | snap confinement remaps `$HOME` | look under `~/snap/code/<rev>/` |
| `std::thread` link errors (potential) | an ARM bare-metal kit selected | select `GCC 11.4.0 x86_64-linux-gnu` |

Pattern worth internalising: **most of these fail silently or with an error naming
the wrong layer.** When something doesn't work, ask *which layer* — editor,
extension, generator, build tool, compiler, or runtime.

---

# PART 2 — SETUP GUIDE

Steps to build this environment from nothing. Assumes Ubuntu/Debian.

## Step 1 — Install the toolchain

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build gdb clang-format
```

`build-essential` pulls in `gcc`, `g++`, `make` and libc headers. Verify:

```bash
g++ --version && cmake --version && ninja --version && gdb --version && clang-format --version
```

Install the two VS Code extensions: **C/C++** (`ms-vscode.cpptools`) and
**CMake Tools** (`ms-vscode.cmake-tools`).

> If any other C/C++ formatter extension is installed (astyle, Clang-Format,
> Beautify), either uninstall it or be sure to complete Step 4's
> `editor.defaultFormatter` — otherwise formatting fails silently.

## Step 2 — Lay out the project

One folder per exercise, each with its own `CMakeLists.txt`:

```
project-root/
├── .clang-format
├── .gitignore
├── CMakeLists.txt
├── .vscode/{settings,tasks,launch}.json
└── chNN_topic/
    └── component/
        ├── CMakeLists.txt
        ├── include/component.hpp
        └── tests/component_driver.cpp
```

## Step 3 — Write the CMake files

**Root `CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.16)

# Pin the native compiler. If a cross-compiler (e.g. arm-none-eabi) is on PATH,
# CMake may cache it and break linking. NOTE: this guard does NOT protect the
# CMake Tools path, because a kit passes -DCMAKE_CXX_COMPILER explicitly.
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER /usr/bin/g++)
endif()
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER /usr/bin/gcc)
endif()

project(my_project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # -> compile_commands.json for IntelliSense

option(ENABLE_TSAN "Build with ThreadSanitizer" ON)
if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g -O1)   # instrumentation + debug info
  add_link_options(-fsanitize=thread)             # must ALSO be on the link line
endif()

add_subdirectory(chNN_topic/component)
```

**Per-component `CMakeLists.txt`** (header-only library + its driver):

```cmake
add_library(component INTERFACE)                  # header-only: nothing compiled
target_include_directories(component INTERFACE include)

add_executable(component_driver tests/component_driver.cpp)
target_link_libraries(component_driver PRIVATE component)
```

**`.gitignore`** — the build dir must never be committed:

```
build/
compile_commands.json
```

## Step 4 — Configure formatting

Generate a starting style file, then edit it:

```bash
clang-format --style=WebKit --dump-config > .clang-format   # or LLVM / Google / Mozilla
clang-format --dump-config > /dev/null && echo "config parses OK"
```

Create **`.vscode/settings.json`**:

```jsonc
{
    // Required when more than one extension can format C++, or VS Code
    // silently does nothing on save.
    "[cpp]": { "editor.defaultFormatter": "ms-vscode.cpptools" },
    "[c]":   { "editor.defaultFormatter": "ms-vscode.cpptools" },
    "editor.formatOnSave": true,
    "C_Cpp.formatting": "clangFormat",
    "C_Cpp.clang_format_style": "file",          // walk up to .clang-format
    "C_Cpp.clang_format_path": "/usr/bin/clang-format",  // pin: cpptools bundles its own

    "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json",
    "C_Cpp.default.cppStandard": "c++17",

    "cmake.sourceDirectory": "${workspaceFolder}",
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "cmake.configureOnOpen": true,   // REQUIRED: populates the File API codemodel

    // Do NOT set "cmake.generator" — see Step 8.

    "files.associations": { "*.hpp": "cpp" }
}
```

## Step 5 — Write the build tasks

**`.vscode/tasks.json`** — note `-G Ninja` on *every* task so nothing ever
disagrees with CMake Tools' default:

```jsonc
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "cmake build",
            "type": "shell",
            "command": "cmake -S ${workspaceFolder} -B ${workspaceFolder}/build -G Ninja && cmake --build ${workspaceFolder}/build -j",
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "cmake build (no TSAN)",
            "type": "shell",
            "command": "cmake -S ${workspaceFolder} -B ${workspaceFolder}/build -G Ninja -DENABLE_TSAN=OFF && cmake --build ${workspaceFolder}/build -j",
            "group": "build",
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "clang-format all",
            "type": "shell",
            "command": "find ${workspaceFolder}/ch* -name '*.[ch]pp' -print0 | xargs -0 -r /usr/bin/clang-format -i --style=file",
            "problemMatcher": []
        }
    ]
}
```

## Step 6 — Write the debug config

**`.vscode/launch.json`**:

```jsonc
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug selected CMake target",
            "type": "cppdbg",
            "request": "launch",
            "program": "${command:cmake.launchTargetPath}",  // needs the File API codemodel
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [
                { "name": "TSAN_OPTIONS", "value": "halt_on_error=0:second_deadlock_stack=1" }
            ],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "/usr/bin/gdb",
            "preLaunchTask": "cmake build",          // must match a tasks.json label
            "setupCommands": [
                { "description": "Pretty-print STL containers",
                  "text": "-enable-pretty-printing", "ignoreFailures": true },
                { "description": "Don't stop on thread lifecycle signals",
                  "text": "handle SIGCHLD nostop noprint pass", "ignoreFailures": true }
            ]
        }
    ]
}
```

**Choosing how `program` is resolved** — two valid strategies:

- **`${command:cmake.launchTargetPath}`** (used here). Works with CMake's default
  mirrored output layout. Requires CMake Tools to have configured, and requires you
  to pick a target in the status bar. Best when you edit headers more than drivers.
- **`${fileDirname}/${fileBasenameNoExtension}`** (used by the sibling DSA project).
  No target picker at all — but it only works if the binary sits next to its source,
  which you must arrange in CMake:

  ```cmake
  set_target_properties(${t} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${src_dir}
      OUTPUT_NAME ${name})
  ```

  Best when every `.cpp` has its own `main()` and you always press F5 from that file.

## Step 7 — Fix the TSAN/ASLR incompatibility

Required once per machine, or TSAN binaries abort before `main()`:

```bash
sudo sysctl -w vm.mmap_rnd_bits=28
echo 'vm.mmap_rnd_bits=28' | sudo tee /etc/sysctl.d/99-tsan.conf   # persist
```

Without root, prefix ad hoc runs with `setarch -R`. F5 is unaffected either way.

## Step 8 — First configure and kit selection

**Order matters.**

1. If `build/` already exists from experimentation, **delete it** —
   generator and compiler are baked into the cache:
   ```bash
   rm -rf build
   ```
2. Reload the VS Code window (`Developer: Reload Window`), especially if CMake
   Tools has errored — it does not recover from a faulted driver on its own.
3. Command palette -> **CMake: Select a Kit** -> pick the **native x86_64** entry
   (e.g. `GCC 11.4.0 x86_64-linux-gnu`). Never a `arm-none-eabi` kit.
   If the list is stale, run **CMake: Scan for Kits** first.
4. Command palette -> **CMake: Configure**. This writes the File API query and
   codemodel that the target pickers need.
5. Set the debug target in the status bar (or via **CMake: Set Debug Target**).

> **Do not set `"cmake.generator"`.** Letting CMake Tools use its Ninja default is
> what works; pinning it produced `No usable generator found` and disabled kit
> selection entirely. Keep the generators aligned from the *other* side — by
> passing `-G Ninja` in `tasks.json`, as in Step 5.

## Step 9 — Verify, layer by layer

Run these in order; each isolates one layer, so the first failure tells you where
the problem is.

```bash
# 1. Compiler toolchain
g++ --version

# 2. Configure + generate (mirrors what CMake Tools runs)
rm -rf build && cmake -S . -B build -G Ninja

# 3. The right compiler and generator got cached
grep -E "^CMAKE_GENERATOR:INTERNAL|^CMAKE_CXX_COMPILER" build/CMakeCache.txt

# 4. Build
cmake --build build -j

# 5. IntelliSense input exists and names the expected compiler
python3 -c "import json;print(json.load(open('build/compile_commands.json'))[0]['command'][:120])"

# 6. TSAN really is linked
ldd build/path/to/driver | grep tsan

# 7. It runs (setarch -R only needed if Step 7 is unapplied)
setarch -R build/path/to/driver

# 8. Debugger works on it
gdb -batch -ex run --args build/path/to/driver

# 9. Formatting is clean (no output == clean)
clang-format --dry-run --style=file path/to/file.hpp
```

In the editor, confirm: **CMake Tools' File API exists** (this is the one that
silently breaks target pickers):

```bash
ls build/.cmake/api/v1/query/    # must contain 'client-vscode'
ls build/.cmake/api/v1/reply/    # must contain codemodel-v2-*.json
```

If `client-vscode` is missing, CMake Tools has never configured — go back to
Step 8.4.

## Step 10 — Condensed checklist for a brand-new project

1. `apt install build-essential cmake ninja-build gdb clang-format`; install
   cpptools + CMake Tools; remove rival formatter extensions.
2. Create the tree; write root + per-component `CMakeLists.txt`;
   set `CMAKE_EXPORT_COMPILE_COMMANDS ON`.
3. `.gitignore` -> `build/`.
4. `clang-format --style=WebKit --dump-config > .clang-format`.
5. `.vscode/settings.json` — `formatOnSave`, `[cpp].defaultFormatter`,
   pinned `clang_format_path`, `configureOnOpen: true`, **no `cmake.generator`**.
6. `.vscode/tasks.json` — build task with `-G Ninja` and `"$gcc"` matcher.
7. `.vscode/launch.json` — `cppdbg`, `preLaunchTask` matching the task label,
   pretty-printing on.
8. `sudo sysctl -w vm.mmap_rnd_bits=28` if using sanitizers.
9. Reload window -> Select Kit (native!) -> Configure -> Set Debug Target.
10. Walk Step 9's verification list.

---

## Glossary

| Term | One-line meaning |
|---|---|
| preprocessor | Stage 1: text substitution — `#include`, `#define`, `#if` |
| translation unit | One `.cpp` after all headers are pasted in; the compiler's unit of work |
| object file (`.o`) | Machine code with unresolved holes + a symbol table; not runnable |
| symbol | A named entity in an object file: defined (`T`) or undefined (`U`) |
| declaration / definition | "this exists" (compiler needs it) / the actual body (linker needs it) |
| ODR | One Definition Rule — exactly one definition per program; templates/`inline` exempt |
| linker | Stage 4: matches undefined symbols to definitions, patches addresses |
| `undefined reference` | A *link*-stage error: used but never defined, or library not linked |
| static library (`.a`) | Archive of `.o` files, copied into the executable at link time |
| shared library (`.so`) | Kept separate; resolved by the loader at program start |
| header-only | No `.o` at all; code re-processed in every translation unit |
| loader (`ld.so`) | Stage 5: maps the binary into memory at run time |
| DWARF | Debug-info format `-g` embeds; what GDB reads |
| driver | A program that orchestrates other programs (`g++` runs cc1plus, as, ld) |
| GNU | Free-software project supplying the compiler, debugger, make, libc |
| GCC | GNU Compiler Collection — the compiler project as a whole |
| `gcc` / `g++` | Driver programs; `g++` links libstdc++ automatically |
| toolchain | compiler + assembler + linker + libc + libstdc++ |
| cross-compiler | Builds for a *different* machine (e.g. `arm-none-eabi` = bare metal ARM) |
| hosted / freestanding | With an OS underneath / without one |
| CMake | Build-system *generator* |
| generator | Which build tool's files CMake emits (Ninja, Unix Makefiles) |
| CMakeCache.txt | Sticky configure results; compiler + generator locked in here |
| target | A buildable thing (executable, library) plus its usage requirements |
| INTERFACE library | Metadata-only target; the model for header-only code |
| out-of-source build | All artifacts in a disposable `build/` dir |
| `compile_commands.json` | Exact compiler invocation per file; feeds IntelliSense |
| CMake File API | `build/.cmake/api/` protocol; feeds CMake Tools' target pickers |
| codemodel | The File API's JSON description of all targets |
| Make / Ninja | Build tools: dependency graph + minimal rebuild |
| LLVM / Clang | Compiler infrastructure / its C++ front end — separate from GCC |
| clang-format | Standalone formatter; needs no build system, works with GCC builds |
| `.clang-format` | YAML style rules, found by walking up the tree |
| GDB | GNU debugger; needs `-g` (DWARF) debug info |
| DAP | Debug Adapter Protocol — VS Code <-> debug adapter |
| GDB/MI | GDB's machine interface — adapter <-> gdb |
| `cppdbg` | cpptools' debug adapter, the bridge between those two |
| pretty-printer | Renders `std::vector` etc. readably in the debugger |
| sanitizer | Compile-time instrumentation + runtime library for bug detection |
| TSAN | ThreadSanitizer — finds data races |
| shadow memory | TSAN's per-byte metadata; why fixed address ranges are needed |
| ASLR | Address space randomization; conflicts with TSAN, disabled by GDB |
| kit | CMake Tools' record of one toolchain (name + compiler paths) |
| JSONC | JSON with comments — the `.vscode/*.json` dialect |
| problemMatcher | Regex turning compiler output into clickable Problems entries |
| snap | Containerized package format; remaps `$HOME` to `~/snap/<app>/<rev>/` |
