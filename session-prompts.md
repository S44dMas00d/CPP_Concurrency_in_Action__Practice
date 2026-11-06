# Session prompts

Copy-paste prompts. They work in a new chat or an existing one: the rules live in the framework, and each prompt makes Claude re-read the files from disk.

## 1. New section

```
New worksheet for section <X.Y.Z>. Re-read concurrency-learning-framework.md, concurrency-progress-ledger.md and this section's notes file from disk, even if you've read them in this chat. Run Session protocol steps 1–5, then stop and wait for my answers.

Section text:
<paste>
```

## 2. Review

```
Review my answers for section <X.Y.Z>. Re-read concurrency-learning-framework.md, concurrency-progress-ledger.md and the notes file from disk, even if you've read them in this chat. Run Session protocol steps 7–9. Don't edit my answers, and don't commit.
```

## 3. Re-verify a fix

```
I've fixed <F-id>. Re-read its row in concurrency-progress-ledger.md and the original review in the notes file. Run tools/verify.sh on it, check only what the row asks for, and mark it done if it passes. Don't commit.
```

Review-only day (backlog, no new section): use prompt 1 with no section text and say "review-only worksheet".
