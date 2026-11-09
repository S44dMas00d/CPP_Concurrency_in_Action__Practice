# C++ Concurrency in Action: practice repo

This is a repo for practicing *C++ Concurrency in Action* (2nd edition) by Anthony Williams.

Reading the book wasn't enough to make it stick, so I went through it section by section. For each section I answered questions closed-book, then rebuilt the section's main idea as a small reusable header, with a test program that hammers it from several threads. Every build was run under ThreadSanitizer and AddressSanitizer. The questions answered wrong come back later as warm-ups, so the weak spots keep getting revisited.

## What's inside

- **26 small builds**, one per mechanism. They start with a thread guard and a thread-safe queue, move through atomics, spinlocks, lock-based and lock-free stacks, queues and hash tables, and end with parallel algorithms and a work-stealing thread pool.
- **Notes for each section:** the questions, my answers as I wrote them (mistakes included), and the reviews.
- **A short write-up of the method** in `concurrency-learning-framework.md`, if you want to study a book the same way.

Each chapter has its own folder. Inside it, each build has an `include` folder for the header and a `tests` folder for the test program.

## Running it

It needs Linux, GCC with C++17, and CMake. To build everything:
```
cmake -S . -B build && cmake --build build
```

## Status

Chapters 2 to 9 are done, apart from a few descriptive sections I skipped and three sections I haven't reached yet. Not every build is perfect: the open issues are listed in `concurrency-progress-ledger.md`.

## A note on AI

I used Claude as a strict examiner, not to write code. It set the questions and build tasks, graded my work and pointed out my mistakes. The answers and the code are mine.

## Thanks

The code in this repo closely follows the listings in *C++ Concurrency in Action* by Anthony Williams. I wrote it from memory as a study exercise, but the designs and most of the code are his, and all credit for them goes to him. This repo is shared for learning purposes only. If you are the author or publisher and would like anything changed or removed, please open an issue and I'll take care of it.
Thank you to Anthony Williams for writing the book. It's the best book on the subject I've read, and none of this would exist without it.
