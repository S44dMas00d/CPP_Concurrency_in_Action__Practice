#!/usr/bin/env python3
"""Review-queue scheduler for the C++ Concurrency retrieval-practice repo.

The schedule lives here, in code, so it doesn't depend on anyone's memory.
State is review-queue.csv at the repo root. Coverage and the fix list are
read from concurrency-progress-ledger.md for lint checks only.

Commands (run from anywhere):
  python3 tools/review.py due                 # what to ask today + lint
  python3 tools/review.py log ID hit|miss [--note "..."]
  python3 tools/review.py add SECTION ID "concept" --kind core|miss|misconception [--source "..."]
                                               # --source is required for miss/misconception
  python3 tools/review.py edit ID [--concept ..] [--kind ..] [--source ..] [--due ..] --note "why"
  python3 tools/review.py drop ID --reason "why"   # deletes the row (e.g. out-of-book item)
  python3 tools/review.py list [--section 4.2] [--all] [-v]
  python3 tools/review.py stats
  python3 tools/review.py lint

Global option: --date YYYY-MM-DD pretends "today" is that date (testing only).

Scheduling rules (successive relearning, date-based):
  new core item          -> due in 2 days
  new miss/misconception -> due in 1 day
  hit on an active item  -> streak+1; next gap 5 days (streak 1), 12 days
                            (streak 2); streak 3 -> retired, maintenance
                            check in 30 days
  miss (any item)        -> streak 0, active, due in 2 days
  hit on a retired item  -> stays retired, next maintenance check in 60 days
  logging an item before it's due: a miss still counts (resets), a hit is
  recorded in history but NOT banked (a same-day re-ask isn't spaced).

Retrieval is gated only by date. A section's status (open corrections, an
unclosed subsection) never keeps its items out of the warm-up. Each miss item
carries a `source` (notes file + question) that `due` prints, so the original
answer is easy to find and correct.
"""

import argparse
import csv
import datetime as dt
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUEUE = os.path.join(ROOT, "review-queue.csv")
LEDGER = os.path.join(ROOT, "concurrency-progress-ledger.md")

FIELDS = ["id", "section", "kind", "concept", "source", "streak", "next_due", "status", "history"]
KINDS = ("misconception", "miss", "core")
KIND_RANK = {"misconception": 0, "miss": 1, "core": 2}
NEW_GAP = {"core": 2, "miss": 1, "misconception": 1}
HIT_GAP = {1: 5, 2: 12}  # gap after reaching this streak
RETIRE_AT = 3
MAINT_FIRST = 30
MAINT_NEXT = 60
MISS_GAP = 2
OVERDUE_WARN_DAYS = 7


# --------------------------------------------------------------------- io
def parse_date(s):
    return dt.date.fromisoformat(s)


def load_queue():
    if not os.path.exists(QUEUE):
        return []
    with open(QUEUE, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        r["streak"] = int(r["streak"] or 0)
        r["source"] = r.get("source") or ""
    return rows


def section_key(s):
    return tuple(int(p) if p.isdigit() else p for p in re.split(r"[.]", s))


def save_queue(rows):
    rows = sorted(rows, key=lambda r: (section_key(r["section"]), r["id"]))
    fd, tmp = tempfile.mkstemp(dir=ROOT, prefix=".review-queue.", suffix=".tmp")
    with os.fdopen(fd, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in FIELDS})
    os.replace(tmp, QUEUE)


def load_ledger():
    """Returns (coverage: {section: status}, open_fixes: [str])."""
    coverage, open_fixes = {}, []
    if not os.path.exists(LEDGER):
        return coverage, open_fixes
    with open(LEDGER, encoding="utf-8") as f:
        for line in f:
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) >= 3 and re.fullmatch(r"\d+\.\d+", cells[0]):
                coverage[cells[0]] = cells[2].strip("*` ").lower()
            elif len(cells) >= 4 and re.fullmatch(r"F\d+", cells[0]):
                if cells[-1].strip("*` ").lower() == "open":
                    open_fixes.append(f"{cells[0]} ({cells[1]}): {cells[2]}")
    return coverage, open_fixes


def append_history(r, today, text):
    entry = f"{today.isoformat()} {text}"
    r["history"] = f"{r['history']}; {entry}" if r["history"] else entry


def source_hint(r):
    """Where the original answer lives. Falls back to the first history entry
    (migrated items record their origin there as 'prior: ...')."""
    if r["source"]:
        return r["source"]
    first = re.split(r"; (?=\d{4}-\d{2}-\d{2} )", r["history"])[0]
    return (first[:157] + "...") if len(first) > 160 else first


# ------------------------------------------------------------------ logic
def is_due(r, today):
    return parse_date(r["next_due"]) <= today


def priority(r, today):
    overdue = (today - parse_date(r["next_due"])).days
    return (KIND_RANK[r["kind"]], -overdue, r["id"])


def lint(rows, today, quiet_ok=False, verbose_fixes=True):
    coverage, open_fixes = load_ledger()
    warnings = []
    ids = [r["id"] for r in rows]
    dups = sorted({i for i in ids if ids.count(i) > 1})
    if dups:
        warnings.append(f"duplicate ids: {', '.join(dups)}")
    if not coverage:
        warnings.append("could not read the coverage table from the ledger")
    sections_with_items = {r["section"] for r in rows}
    for sec, status in sorted(coverage.items(), key=lambda kv: section_key(kv[0])):
        if status == "done" and sec not in sections_with_items:
            warnings.append(f"section {sec} is 'done' but has no queue items -- add 2-4 core items")
    for sec in sorted(sections_with_items - set(coverage), key=section_key):
        warnings.append(f"queue has items for section {sec}, which is not in the ledger's coverage table")
    current = [s for s, st in coverage.items() if st == "current"]
    if len(current) > 1:
        warnings.append(f"more than one section marked 'current': {', '.join(current)}")
    very_late = []
    for r in rows:
        if r["kind"] not in KINDS:
            warnings.append(f"{r['id']}: unknown kind '{r['kind']}'")
        if r["status"] == "active":
            late = (today - parse_date(r["next_due"])).days
            if late > OVERDUE_WARN_DAYS:
                very_late.append((late, r["id"]))
    if very_late:
        very_late.sort(reverse=True)
        oldest = ", ".join(f"{i} ({d}d)" for d, i in very_late[:3])
        warnings.append(f"{len(very_late)} item(s) more than {OVERDUE_WARN_DAYS} days overdue "
                        f"(oldest: {oldest}) -- run a review-only worksheet to clear the backlog")
    try:
        out = subprocess.run(
            ["git", "--no-optional-locks", "status", "--porcelain"],
            cwd=ROOT, capture_output=True, text=True, timeout=10,
        ).stdout
        dirty = [l[3:] for l in out.splitlines()
                 if l[3:].endswith(("-notes.md", "review-queue.csv", "concurrency-progress-ledger.md"))]
        if dirty:
            warnings.append("uncommitted study-state changes (commit after each review): " + ", ".join(dirty))
    except Exception:
        pass
    if verbose_fixes:
        print(f"Open fix-list items: {len(open_fixes)}")
        for fx in open_fixes:
            print(f"  - {fx}")
    else:
        ids = ", ".join(fx.split(" ", 1)[0] for fx in open_fixes)
        print(f"Open fix-list items: {len(open_fixes)}" + (f" ({ids}) -- details: review.py lint" if open_fixes else ""))
    if warnings:
        print("LINT WARNINGS:")
        for w in warnings:
            print(f"  ! {w}")
    elif not quiet_ok:
        print("Lint: OK")
    return warnings


# --------------------------------------------------------------- commands
def cmd_due(args, today):
    rows = load_queue()
    active = [r for r in rows if r["status"] == "active"]
    due = sorted([r for r in active if is_due(r, today)], key=lambda r: priority(r, today))
    maint = sorted([r for r in rows if r["status"] == "retired" and is_due(r, today)],
                   key=lambda r: r["next_due"])
    upcoming = [r for r in active if today < parse_date(r["next_due"]) <= today + dt.timedelta(days=3)]
    overdue = [r for r in due if parse_date(r["next_due"]) < today]

    print(f"Review queue -- {today.isoformat()} ({today.strftime('%a')})")
    print(f"Due now: {len(due)} active ({len(overdue)} overdue) | maintenance due: {len(maint)} | "
          f"due in next 3 days: {len(upcoming)} | total active: {len(active)}, retired: {len(rows) - len(active)}")
    n = 3 if len(due) > 5 else 2
    picks = due[:n]
    if not picks and maint:
        picks, maint = maint[:1], maint[1:]
    print()
    if picks:
        print(f"WARM-UP PICKS ({len(picks)}) -- ask these, rephrased fresh, closed-book:")
        for i, r in enumerate(picks, 1):
            late = (today - parse_date(r["next_due"])).days
            lateness = f"{late}d overdue" if late > 0 else "due today"
            print(f"  {i}. [{r['kind']}] {r['id']}  (sec {r['section']}, {lateness}, streak {r['streak']}, {r['status']})")
            print(f"     probe: {r['concept']}")
            print(f"     source: {source_hint(r)}")
    else:
        print("WARM-UP: nothing due -> ask ONE cross-chapter design-judgment question,")
        print("  then record it: review.py add <newest section it touches> <id> \"<concept>\" --kind core")
        print("  (use --kind miss instead if the answer missed).")
    rest = due[len(picks):]
    if rest:
        print(f"\nAlso due (next worksheets): " + ", ".join(r["id"] for r in rest))
    if maint:
        print("Maintenance checks due (only if a warm-up slot is spare): " + ", ".join(r["id"] for r in maint))
    print()
    lint(rows, today, verbose_fixes=False)
    print("\nAfter grading: python3 tools/review.py log <id> hit|miss --note \"<one line>\"")


def cmd_log(args, today):
    rows = load_queue()
    r = next((x for x in rows if x["id"] == args.id), None)
    if r is None:
        sys.exit(f"no item with id '{args.id}' (see: review.py list --all)")
    note = f" ({args.note})" if args.note else ""
    early = not is_due(r, today)
    if args.result == "miss":
        r["streak"] = 0
        r["status"] = "active"
        r["next_due"] = (today + dt.timedelta(days=MISS_GAP)).isoformat()
        append_history(r, today, f"miss{note}")
        msg = f"{r['id']}: miss -> streak 0, due {r['next_due']}"
    elif early:
        append_history(r, today, f"early-hit-not-banked{note}")
        msg = f"{r['id']}: hit recorded but NOT banked (item wasn't due until {r['next_due']})"
    elif r["status"] == "retired":
        r["next_due"] = (today + dt.timedelta(days=MAINT_NEXT)).isoformat()
        append_history(r, today, f"maint-hit{note}")
        msg = f"{r['id']}: maintenance hit -> stays retired, next check {r['next_due']}"
    else:
        r["streak"] += 1
        if r["streak"] >= RETIRE_AT:
            r["status"] = "retired"
            r["next_due"] = (today + dt.timedelta(days=MAINT_FIRST)).isoformat()
            msg = f"{r['id']}: hit -> streak {r['streak']} -> RETIRED, maintenance check {r['next_due']}"
        else:
            r["next_due"] = (today + dt.timedelta(days=HIT_GAP[r["streak"]])).isoformat()
            msg = f"{r['id']}: hit -> streak {r['streak']}, next due {r['next_due']}"
        append_history(r, today, f"hit{note}")
    save_queue(rows)
    print(msg)


def cmd_add(args, today):
    rows = load_queue()
    if any(r["id"] == args.id for r in rows):
        sys.exit(f"id '{args.id}' already exists -- log against it instead, or pick a new id")
    if not re.fullmatch(r"\d+\.\d+", args.section):
        sys.exit("section must be a top-level section like 4.2 (put the subsection in the id/concept)")
    coverage, _ = load_ledger()
    if coverage and args.section not in coverage:
        print(f"warning: section {args.section} is not in the ledger's coverage table")
    if args.kind in ("miss", "misconception") and not args.source:
        sys.exit("--source is required for miss/misconception items "
                 "(e.g. --source \"atomic-operations-and-types-notes.md 5.2.3 Q1\")")
    due = parse_date(args.due) if args.due else today + dt.timedelta(days=NEW_GAP[args.kind])
    rows.append({
        "id": args.id, "section": args.section, "kind": args.kind, "concept": args.concept,
        "source": args.source,
        "streak": 0, "next_due": due.isoformat(), "status": "active",
        "history": f"{today.isoformat()} added{(' (' + args.note + ')') if args.note else ''}",
    })
    save_queue(rows)
    print(f"added {args.id} [{args.kind}] sec {args.section}, due {due.isoformat()}")


def cmd_edit(args, today):
    rows = load_queue()
    r = next((x for x in rows if x["id"] == args.id), None)
    if r is None:
        sys.exit(f"no item with id '{args.id}' (see: review.py list --all)")
    changed = []
    for field in ("concept", "kind", "source"):
        val = getattr(args, field)
        if val is not None and val != r[field]:
            r[field] = val
            changed.append(field)
    if args.due:
        r["next_due"] = parse_date(args.due).isoformat()
        changed.append(f"due->{r['next_due']}")
    if not changed:
        sys.exit("nothing to change")
    append_history(r, today, f"edited {', '.join(changed)} ({args.note})")
    save_queue(rows)
    print(f"{r['id']}: edited {', '.join(changed)}")


def cmd_drop(args, today):
    rows = load_queue()
    r = next((x for x in rows if x["id"] == args.id), None)
    if r is None:
        sys.exit(f"no item with id '{args.id}' (see: review.py list --all)")
    rows.remove(r)
    save_queue(rows)
    print(f"dropped {r['id']} [{r['kind']}] sec {r['section']} -- reason: {args.reason}")
    print(f"  (was: {r['concept']})")


def cmd_list(args, today):
    rows = load_queue()
    for r in rows:
        if args.section and r["section"] != args.section:
            continue
        if not args.all and r["status"] != "active":
            continue
        flag = "DUE" if is_due(r, today) else "   "
        print(f"{flag} {r['next_due']}  s{r['streak']} {r['status']:<7} [{r['kind']:<13}] {r['id']}")
        if args.verbose:
            print(f"      probe: {r['concept']}")
            print(f"      source: {source_hint(r)}")
            print(f"      history: {r['history']}")


def cmd_stats(args, today):
    rows = load_queue()
    by = {}
    for r in rows:
        b = by.setdefault(r["section"], {"active": 0, "retired": 0, "due": 0, "streak_sum": 0})
        b[r["status"]] += 1
        b["streak_sum"] += r["streak"]
        if r["status"] == "active" and is_due(r, today):
            b["due"] += 1
    print(f"{'sec':<6}{'active':>7}{'retired':>8}{'due':>5}{'mastery':>9}")
    for sec in sorted(by, key=section_key):
        b = by[sec]
        total = b["active"] + b["retired"]
        mastery = b["streak_sum"] / (RETIRE_AT * total) if total else 0
        print(f"{sec:<6}{b['active']:>7}{b['retired']:>8}{b['due']:>5}{mastery:>8.0%}")
    print("(mastery = banked spaced hits / hits needed to retire every item in the section)")


def cmd_lint(args, today):
    warnings = lint(load_queue(), today)
    sys.exit(1 if warnings else 0)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--date", help="override today's date (YYYY-MM-DD), for testing")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("due")
    pl = sub.add_parser("log")
    pl.add_argument("id")
    pl.add_argument("result", choices=["hit", "miss"])
    pl.add_argument("--note", default="")
    pa = sub.add_parser("add")
    pa.add_argument("section")
    pa.add_argument("id")
    pa.add_argument("concept")
    pa.add_argument("--kind", choices=KINDS, required=True)
    pa.add_argument("--due", help="explicit due date YYYY-MM-DD (default: by kind)")
    pa.add_argument("--note", default="")
    pa.add_argument("--source", default="", help="notes file + question, e.g. 'x-notes.md 5.2.3 Q1'")
    pe = sub.add_parser("edit")
    pe.add_argument("id")
    pe.add_argument("--concept")
    pe.add_argument("--kind", choices=KINDS)
    pe.add_argument("--source")
    pe.add_argument("--due", help="new due date YYYY-MM-DD")
    pe.add_argument("--note", required=True, help="why (recorded in history)")
    pd = sub.add_parser("drop")
    pd.add_argument("id")
    pd.add_argument("--reason", required=True)
    pli = sub.add_parser("list")
    pli.add_argument("--section")
    pli.add_argument("--all", action="store_true", help="include retired items")
    pli.add_argument("-v", "--verbose", action="store_true")
    sub.add_parser("stats")
    sub.add_parser("lint")
    args = p.parse_args()
    today = parse_date(args.date) if args.date else dt.date.today()
    {"due": cmd_due, "log": cmd_log, "add": cmd_add, "edit": cmd_edit, "drop": cmd_drop, "list": cmd_list,
     "stats": cmd_stats, "lint": cmd_lint}[args.cmd](args, today)


if __name__ == "__main__":
    main()
