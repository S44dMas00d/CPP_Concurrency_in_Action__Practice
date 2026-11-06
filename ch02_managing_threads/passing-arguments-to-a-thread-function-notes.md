# 2.2 — Passing arguments to a thread function

Session 3.

Warm-up (priority: misconceptions log first, since it's the only thing actually eligible — 4.1 is nominally "due" by the session count, but it was never self-checked or closed out the first time, so there's nothing genuinely banked to space-retrieve yet; resurfacing it as a warm-up would just be re-asking an unanswered question, not retrieval practice. Still open, whenever you get to it.):

0. Quick one: if you call `t.detach()` twice on the same `std::thread` object, what actually happens at runtime — and how is that different from what happens if a joinable `std::thread`'s destructor runs without `join()`/`detach()` ever being called?

Answer: calling std::detach() twice on a thread object results in a std::system_error because once detached - the thread loses association with the underlying thread. join and detach are meant to dissociate the thread object from the underlying thread before its destroyed, if a thread is still joinable, i.e., the underlying thread is still associated to it and working, and the object is destroyed, then the std::terminate() is called and the resources are deallocated no matter where the thread was with its task.


No build task this section — it's argument-passing semantics and gotchas, not a reusable data structure or pattern, so per the triage this stays quiz-only.

## Quiz — closed-book, tag each answer's confidence (low/med/high) before checking against the book

Recall:
1. By default, how does `std::thread`'s constructor pass additional arguments through to the callable — copied or by reference — and does that depend on what the target function's parameter type actually is?

Answer (med): by default, arguments to the thread's constructor are passed as copies. It does not depend on the parameter's type - the thread's constructor tries to copy the argument even if the function wrote func(obj_type& obj).

2. Why does wrapping a value in `std::ref()` fix the "wanted a reference, got a copy" problem? What is `std::ref` actually doing?

Answer (med): If your thread passes a copy even though the function expects a reference like in here - func(obj_type& obj) - then we use std::ref() to force pass-by-reference for that argument. std::ref actually passes the argument by reference and avoids the copy passing bias of the thread constructor since the thread constructor often doesn't know that the function expects a reference and not a copy.

3. What mechanism does `std::thread`'s constructor share with `std::bind`, and what capability does that give you (think: what else can you pass as the "function")?

Answer (med): i dont know much about std::bind; however, what i do know is that the shared behavior allows us to give a function pointer to the thread constructor as its callable and if we want to give a class method as a callable we need to give an object pointer as the first argument after the function / method pointer, so if I have a class X and an object of it myX, and its method - method1, then i can make the thread - std::thread t1(&X::method1, &myX).

4. For a move-only type like `std::unique_ptr`, when is `std::move()` required vs. automatic when passing it into `std::thread`'s constructor?

Answer (med): for a move only type of object, its copy constructor and assignment constructor is deleted and so it cant be passed to the thread constructor as a copy, its ownership must be changed to an lvalue inside the thread callable - for Rvalues, this ownership acquisition in a callable is implicit; however, for a move only object already associated with an lvalue to then change hands with another lvalue, we need std::move() to make that change explicitly. 

**Correction - 2026-10-05**
for a move-only object like unique pointer, the copy assignment operator and copy constructor are deleted and so passing by copy is impossible regardless of wether we pass it in as Rvalue or Lvalue. That being said, we do need to call the move constructor when passing the move only object to the function, we know that move constructors are defined with Rvalues like so - `Buffer(Buffer&& other) {...}` and when we pass an Rvalue to the function, this move constructor gets picked automatically where as when passing an Lvalue, we have to use std::move() to make an Rvalue more explicitly available for this constructor to then be called.

Code-tracing:

5. The `oops()`/`buffer` example: a `char buffer[1024]` is filled with `sprintf`, passed as an argument to a function expecting `std::string`, and the thread is detached. Trace exactly why this is undefined behavior — specifically, *which* thread's context does the `char*` → `std::string` conversion actually happen in, and why does that matter once `detach()` is in the picture?

Answer (high): the char buffer is a character array which gets passed as a char * pointer to the callable f. the conversion from char* to string happens in the context of the newly instantiated thread and not the caller thread, and when we mix in detach() - basically the caller thread just passes the char * pointer to the callable f and detaches and then the scope ends so by the time the thread t actually is constructed and gets to making the conversion from char* to string, the char* may already be a dangling pointer thanks to the calling function going out of scope and destroying the buffer.

**Correction - 2026-10-05**
the above answer seems to have already been corrected and the correction not logged, it looks fine and there is no need for reiteration in this instance.

6. The `oops_again()`/`widget_data` example: `update_data_for_widget` expects `widget_data&`, but the `std::thread` constructor is given `data` without `std::ref`. Trace what actually happens to the updates the background thread makes — where do they end up, and why does `process_widget_data(data)` afterward see unchanged data?

Answer (high): passing data without std::ref() causes it to be copied over to the new thread and this causes the updates to be applied to the copy and not the intended original data object. std::ref() has to be used to pass the data object by reference and make the changes made by the thread actually show up on the original object.

## Review

Warm-up: clean, and it's a real fix — correctly separated `std::system_error` (double `detach()`/`join()` on an already non-joinable thread) from `std::terminate()` (destructor runs while still joinable). Logged as the first clean spaced recall on that misconception; needs two more clean hits before it's retired from the log.

Quiz — 4 solid, 1 partial-but-honest, 1 right-answer-shaky-reasoning, 1 real miss:

1. Correct.
2. Correct — right mechanism, right reason the constructor needs the nudge.
3. Partial, and honestly flagged as such. The practical part — member-function-pointer + object-pointer, with a correct example — is exactly right and is what actually matters for interviews. The "what mechanism is shared" part is genuinely underspecified in the book's own text too (it just says both are "defined in terms of the same mechanism" without naming it further here), so don't read this as a bigger gap than it is — you got everything the book actually gives you.
4. Right conclusion (temporary → automatic move, named value → explicit `std::move()`), matches the book exactly. But flag the reasoning underneath: "there can only be one lvalue referencing it" isn't the right frame, and it's worth fixing before an interview asks "why can't you copy a `unique_ptr`" directly. The actual reason is simpler — `unique_ptr`'s copy constructor is deleted, full stop, regardless of whether the source is an lvalue or rvalue. Move-only means only the move constructor exists; overload resolution picks it automatically for an rvalue (a temporary) and requires `std::move()` to manufacture an rvalue reference from a named (lvalue) source. This is a "high confidence, but recheck the mental model" flag, not a "wrong answer" flag.
5. **Miss, and it's the important part of the example.** You have the direction backwards: the book states the `char*` → `std::string` conversion happens *in the new thread's context*, not the caller's — "the string literal is passed as a char const* and converted to a std::string only in the context of the new thread." The caller thread does almost nothing (copies the raw pointer into internal storage, then `oops()` can return immediately). The actual danger: there's no guarantee the new thread has even started running `f` by the time `oops()` returns and `buffer` gets destroyed — so when the new thread eventually does get scheduled and tries to construct the `std::string` parameter from that pointer, it may already be dangling. It's UB specifically because there's a race with no ordering guarantee, not because the conversion definitely happens too early in some fixed sequence. Correctly self-flagged as "low" — this one was worth the caution.
6. Correct.

Not logging Q5's inversion to the misconceptions list yet — one occurrence, and the pattern (mixing up which side of a thread hand-off does what) isn't clearly the *same* failure mode as the join/detach one. Worth watching if it resurfaces.

## Corrections confirmed

Q4 rewritten around the actual reason (copy ctor/assignment deleted, not "one lvalue"), confidence correctly walked back from high to med. Q5 direction fixed — conversion now correctly placed in the new thread's context, race correctly described. Both now accurate. **Section passable, closed out.**

### Correction check (2026-10-06)

These corrections were written after the review, so they don't count as retrieval and the queue schedule is unchanged. The queue items' `source` lines now point here. Q4: correct (copy operations deleted, so an rvalue picks the move constructor automatically and an lvalue needs `std::move`). Q5 was already corrected in the earlier round (see "Corrections confirmed").

Process note: the original Q3 answer was lightly reworded ("so if your thread…" → "If your thread…"). Original answers stay untouched; put changes under them.
