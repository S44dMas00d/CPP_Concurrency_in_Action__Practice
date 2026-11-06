# C++ Concurrency in Action: Retrieval Practice Framework

**Goal:** closed-book mastery of *C++ Concurrency in Action* (2nd ed., Anthony Williams). The method is retrieval practice, spaced over calendar days, with build-and-test for reusable mechanisms. It is designed for 20–30 minute sittings around a day job.

**Design principle:** anything that has to happen every time is enforced by a script or a required template heading, not by memory. Past slips were all rules that lived only in prose and were dropped late in long conversations (see the Decision history below).

## Where things live

| File | Holds | Changed by |
|---|---|---|
| `review-queue.csv` | The review schedule: one row per concept item | **Only** `tools/review.py` (`add`, `log`, `edit`, `drop`) |
| `concurrency-progress-ledger.md` | Coverage table (section status) and fix list | Claude, at review time |
| `chXX_*/<topic>-notes.md` | Questions, answers, reviews, corrections for one section | Both (answers are never overwritten) |
| `tools/verify.sh` | Build gate: TSAN + ASan/UBSan + two-TU link check | Run on every build review |
| `session-prompts.md` | Copy-paste prompts: new worksheet, review, re-verify a fix | Updated when the protocol changes |

## Session protocol (every worksheet, in this order, no exceptions)

1. **Re-read from disk.** Read this file, the ledger, and the section's notes file, even if this conversation has read them before. Don't work from memory. Start a new conversation when the context degrades.
2. **Run `python3 tools/review.py due`** and paste its output at the top of the worksheet.
3. **Warm-up:** ask exactly the WARM-UP PICKS it lists. Rephrase each probe fresh; never reuse an earlier wording. If it says nothing is due, ask one cross-chapter design-judgment question ("which primitive fits, and what breaks if you pick wrong"), answerable from the book text read so far, then `add` it as an item under the newest section it touches.
4. **Triage** the pasted section (rules below) before writing anything.
5. **Worksheet:** the quiz items. Add a build spec if the section triages to one. Every graded item follows the **book-only rule** (below).
   - Optionally, at most one **Beyond the book** item: material the text doesn't cover. It is labelled as such, ungraded, never blocks anything, and enters the queue only if the user says "queue it".
   - A build spec contains the interface signatures, the thread-safety contract, a **Tests to write** list (see Build tasks), and **always ends with the self-explanation prompt.** Plain wording: use book terms; anything else gets a one-clause definition or is left out.
   - Scaffold the build folder at issue time (per `repo-structure.md`).
6. **You answer closed-book,** with a low/med/high confidence tag on each quiz answer. Your answers are never edited later; corrections go underneath as `Correction (YYYY-MM-DD): …`.
7. **Review**, using the template below.
8. **Queue updates:**
   - `log` every warm-up result.
   - `add` a `miss` item for every quiz miss, with `--source "<notes file> <section> Q<n>"` (the script requires it). `due` prints the source so the original answer is easy to find and correct.
   - When a section or subsection closes, `add` 2–4 `core` items: the concepts someone who only reread would still get wrong.
   - Paste the exact commands into the review.
9. **Ledger:** update the coverage status and the fix list only. Finish with `review.py lint`. **You commit** after the review; `review.py due` flags any uncommitted state.

### Review template (required headings, in order; an empty heading means a missed step)

```
## Review: <section>
### Warm-up        : item id -> hit/miss, one line each
### Quiz           : per question: > quoted key claim from the answer -> verdict (hit/miss) -> correction if any
### Build          : tools/verify.sh summary line, then findings (library vs. driver), stress-test numbers
### Queue updates  : exact review.py add/log commands run
### Fix list       : F-rows opened/closed
### Beyond the book: (optional, ungraded) notes on material the text doesn't cover or states in outdated form
```

**Grading is binary.**
- **hit:** correct on the point being probed. Precision nits are noted but don't count against it.
- **miss:** wrong, missing the probed point, or only half of a two-part question answered.

**Quote your actual claim before judging it.** A past review graded a sentence the answer never contained (4.1 Q7).

### Book-only rule (graded material)

- Every graded question (quiz, warm-up, build contract, self-explanation prompt) must be answerable from the book text read so far. Applying a book mechanism to a trading scenario is fine; needing a fact the book never states is not.
- Material beyond the text goes in the ungraded **Beyond the book** item or review heading. It is never graded and never queued unless the user asks.
- Where the book is outdated (e.g. C++17 dropped the "failure order no stronger than success" restriction on compare/exchange), grade against the book and put the modern rule under Beyond the book.
- If a graded question turns out to have needed outside knowledge, regrade it as ungraded and `drop` its queue item.

## Triage: quiz question or build task

Sort each real failure mode before writing anything:

- **Fact / contract detail** (a name, a guarantee, a "why does X exist"): one **Recall** question.
- **Behavioral gotcha in existing code** (predict why a snippet races, deadlocks, or misbehaves): one **Code-tracing** question against a short snippet.
- **Reusable mechanism** (a data structure or pattern meant for real code, including anything the book derives in-text): **no quiz for it; a build task.**

Rule of thumb: if a well-informed answer fits in one sentence, it's a quiz question. If the honest answer is "you have to build it and watch it break", it's a build.

**Design-judgment questions** are for "which tool fits, and why". One question per real failure mode; no padding, and no fixed count. A failure mode covered by a build is not also quizzed.

A section with no testable content (scene-setting, or a worked example that isn't reusable) is `skipped` with a one-line reason in the ledger. The user decides this at triage time; don't decide it silently.

## Build tasks

- **Claude never writes the implementation**: not in the spec, not in the review, not after a failed attempt.
  - Exception: once an attempt has *correctly diagnosed* a problem and is only missing a peripheral language-rule fact (a container-compatibility rule, an overload quirk), explain that fact directly.
  - Peripheral tangents in drivers are worth pursuing if they're cheap and substantive. Cut them short if they're eating hours.
- **Self-explanation first:** 2–4 closed-book sentences before review, covering why the build is thread-safe and what breaks with the obvious alternative.
- **Tests to write** (in the spec): 2–4 numbered tests in plain English, sized for about 30–45 minutes of driver code. Each gives:
  - the setup: how many threads, roughly how many operations, what values (including any value pattern needed for the bug to be possible at all);
  - the one thing to assert;
  - one line on which bug it catches.

  No test code from Claude.
- **Verification gate:** `tools/verify.sh <task>` (20 runs by default) must print `TSAN N/N clean | ASan+UBSan N/N clean | 2-TU link OK`.
  - Claude also runs an independent stress test in scratch space to check the **library**. A library bug is a real fix, whatever the driver shows.
  - The **driver** is reviewed against the Tests-to-write list only: was each listed test written, and can its assert actually fail? A missing listed test, or an assert that can't fail, is a fix-list item.
  - Driver weaknesses outside the list: a one-line note, not a fix item. Nits (naming, unused includes, warnings): one line in total, never a fix item.
  - Report real run counts, never estimates.
  - Never edit the user's files to test a fix; copy to scratch.
- A build counts as done when it is fixed and re-verified, not when it is reviewed. Structures persist and get reused later (the 4.1 queue feeds 6.2 and 9.1, and so on).
- Occasionally assign a small, bounded extension of an earlier build (one sitting). Full multi-mechanism projects are out of scope.

## Scheduling (implemented by `tools/review.py`; this is only a summary)

- The unit is a **concept item**, not a section.
- New items fall due in 2 days (`core`) or 1 day (`miss` / `misconception`).
- Each **hit** on a due item extends the gap: 5 days, then 12 days. The third spaced hit **retires** the item, with maintenance checks at 30 days and then every 60 days.
- A **miss** resets the streak and makes the item due again in 2 days.
- Retrieval is gated **only by date**. An unclosed section, an open correction, or an unfinished build never keeps an item out of the warm-up. Writing a `Correction` under the original answer is encouraged (the `source` line points to it) but never blocks anything.
- A hit logged before the item is due is recorded but not banked.
- Warm-up load is 2 items per worksheet (3 when more than 5 are due), with misconceptions first, then misses, then the most overdue.
- If more than a few items are more than 7 days overdue, do a **review-only worksheet**: no new section, just the queue.
- There is no session counter. Everything is scheduled by calendar date.

## Route and checkpoints

Chapters 2 through 9, in strict book order, section by section as the user reads. **Chapter 1 is excluded** (user decision, 2026-09-24). **Chapters 10 and 11 are out of the first run** (user decision, 2026-10-26).

**Current exception (user decisions 2026-10-07, 2026-10-16, 2026-10-26):** chapter 7 is paused after 7.2.4. The first run continues with chapters 8 and 9 and ends after chapter 9. When the first run is finished, the backlog is worked through with review-only worksheets, one at a time. That backlog is: the overdue queue, the ch5 corrections, the rest of 5.3, 7.2.5–7.3, and the fix list. Until then review-only worksheets stay paused; warm-ups continue as normal.

Known build checkpoints (a living list; the triage rule decides each section on first read):
- **Built:** 2.1 thread_guard · 2.3 scoped_thread · 2.4 parallel_accumulate · 3.2.3 threadsafe_stack · 3.2.4 account_transfer · 3.3.1 lazy_shared_resource · 3.3.2 shared_mutex_cache · 4.1 threadsafe_queue · 4.2.1 async_exchange_fetch · 4.2.2 task_runner · 4.2.3 pending_requests · 5.2.2 spinlock_mutex · 5.2.3 atomic_fetch_max · 5.2.7 snapshot_cell
- **Ahead:**
  - 6.2/6.3: extend the stack and queue; build a lookup table (and possibly a list).
  - 7.2: lock-free stack/queue.
  - 8.x: possibly one small pipeline exercise.
  - 9.1: thread pool, reusing the ch6 queue.
- Optional: bounded-queue extension of 4.1 (ledger fix list F8).

## Locked decisions

- Full rigor for every chapter covered. The user decides skips at triage time.
- Quiz vs. build is triaged per failure mode, not per chapter.
- ~~**If time runs short,** ch10 (parallel algorithms, execution policies) and then 11.1 are the first candidates to downgrade to quiz-only.~~ Superseded 2026-10-26: chapters 10 and 11 are out of the first run.
- The self-explanation step covers "defend your reasoning".

## Grounding (why this design)

- **Retrieval beats restudy** for durable, transferable learning ("desirable difficulties", Bjork). Building and testing under real concurrent load is the strongest form of it.
- **Successive relearning** (Rawson & Dunlosky): retrieve correctly, then relearn in several spaced sessions. Spaced sessions beat many correct recalls in one sitting, and about three spaced relearning sessions capture most of the benefit. This is the basis for the 3-hit rule, applied per concept.
- **Gap size:** the best gap scales with how long you need to remember (Cepeda et al. 2008). Expanding and equal intervals give the same long-term retention (Kang et al. 2014). What matters most is that the retests actually happen.
- **Interleaving** works through short, mixed "which tool applies" problems (Rohrer & Taylor), not through large integrated builds. That's why cross-chapter practice stays small.
- **Self-explanation** produces transfer gains that receiving an explanation doesn't (Chi et al.). **Confidence tags** direct review time to confident misses.
- **Code tracing** is a distinct skill that predicts code-writing ability, which is why tracing questions are their own category.
- **LLM reliability** degrades as a conversation grows ("context rot", Chroma 2025). Hence scripts, templates, and re-reading state from disk every session.

## Decision history (condensed; details in git history)

- **2026-09-02, kickoff.** Full rigor, strict book order. Session-count spacing, section-level mastery.
- **Sessions 1–16.** The 4.1 pilot was retired and redone in book order at session 15. Several rules were added after silent lapses: the warm-up tier with no due gate, the self-explanation prompt in every spec, quiz numbering restarting in each notes file, and the calendar-date session boundary.
- **2026-09-24, Opus migration**. The audit found:
  - 8 of 12 tested sections had never had a spaced retrieval (exclusion rules plus "in progress" gating).
  - One mis-graded answer (4.1 Q7) and one false spec claim (task_runner abandoned futures).
  - Two missed ODR bugs.
  - The belief that TSAN didn't work, when it does under `setarch -R`.

  What changed:
  - **Replaced:** the per-section due-in/hits columns, the session counter, the misconceptions table, the warm-up tiers, and the exclusion rules.
  - **Introduced:** the per-concept review queue with `review.py`, the verify gate with `verify.sh`, and the fixed review template.
  - **Removed:** Chapter 1, from tracking entirely.
  - Minor driver nits were dropped from the backlog (the `x2.count` assert and `task_runner`'s unused `get_run_task_thread_id`).
- **2026-09-25, pacing fixes after 5.2.3** (user request: keep the rigor, remove unnecessary blocking). 5.2.3 took two days because graded questions needed outside knowledge, and the build's driver was reviewed against a stricter bar than its spec stated.
  - **Book-only rule** for all graded material; an optional, ungraded Beyond the book item (at most one per worksheet), queued only on request.
  - Build specs list **Tests to write** in plain English; drivers are reviewed against that list only.
  - Miss items carry a `source` pointer that `due` prints; `review.py` gained `edit` and `drop`. Confirmed that retrieval was already date-gated only (since the 2026-09-24 migration).
  - Kept: binary grading, spaced retrieval, `verify.sh` gate, self-explanation, and the plan to clear the fix list before ch6.
- **2026-10-07, ch6 moved ahead** (user decision). He does the chapter 6 main quiz run first, then the ch5 corrections and some fix-list items. The review-only trigger is paused until then. This replaces "clear the fix list before ch6". The cost: 27 items were more than 7 days overdue on 2026-10-07, and with 3 warm-ups per worksheet the backlog keeps growing during ch6.
- **2026-10-16, pause extended through ch7** (user decision, after 6.3 closed). He wants to cover ch7 while his last two days of ch5 reading are fresh, and to start the review-only worksheets, ch5 corrections and fixes after ch7. The cost: 62 items were more than 7 days overdue on 2026-10-16 (oldest 20 days), and the backlog keeps growing by about the number of new items per worksheet.
- **2026-10-26, ch7 paused; the first run ends after ch9** (user decision). Chapter 7 was taking too long to absorb (warm-ups blank three worksheets running, earlier chapters fading while 86 items sat more than 7 days overdue), and he wants to finish the book. Ch7 pauses after 7.2.4. Chapters 8 and 9 follow with **the same testing method** (no change to worksheets, builds or grading), and warm-ups are to be taken seriously again. Chapters 10 and 11 are dropped. Afterwards, review-only worksheets clear the backlog one at a time: the queue, the ch5 corrections, the rest of 5.3, 7.2.5–7.3, and the fix list.
