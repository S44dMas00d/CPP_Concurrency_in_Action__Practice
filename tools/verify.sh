#!/usr/bin/env bash
# verify.sh -- the build-review gate for one build task, as one command.
#
#   tools/verify.sh <task_name> [-n RUNS] [-t TIMEOUT_SECS]
#   tools/verify.sh --all      [-n RUNS] [-t TIMEOUT_SECS]
#
# For each task (folder chXX_*/<task_name>/ with include/ and
# tests/<task_name>_driver.cpp) it runs four checks:
#   1. TSAN build (-O1 -g -fsanitize=thread), run RUNS times under
#      `setarch -R` (disables ASLR for the run -- the fix for
#      "FATAL: ThreadSanitizer: unexpected memory mapping"; see
#      build-toolchain-guide.md section 1.8 / Step 7). Counts non-zero exits and
#      "WARNING: ThreadSanitizer" reports.
#   2. ASan+UBSan build (-fno-sanitize-recover=undefined so UB aborts),
#      run RUNS times. Catches lifetime bugs TSAN doesn't (use-after-scope,
#      dangling captures, leaks aren't checked -- detect_leaks=0).
#   3. Two-TU link check: two .cpp files that both #include every header in
#      include/, linked together. "multiple definition" = ODR bug (missing
#      `inline` on a namespace-scope or out-of-class definition).
#   4. Prints ONE summary line to paste into the review, e.g.
#        task_runner: TSAN 20/20 clean | ASan+UBSan 20/20 clean | 2-TU link OK
#
# Builds happen in a temp dir; nothing in the repo (including build/) is touched.
# Exit status: 0 if every check passed, 1 otherwise. First failing run's log
# is kept and its path printed.
#
# Uses /usr/bin/g++ explicitly unless CXX is set (this machine also has an ARM
# cross-compiler on PATH -- see the root CMakeLists.txt).

set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-/usr/bin/g++}"
# Link libatomic when the toolchain has it. GCC implements 16-byte atomics
# (e.g. std::atomic<counted_node_ptr> in 7.2.4) as libatomic calls, so the
# ASan build fails to link without it; it's harmless for every other task.
if echo 'int main(){}' | "$CXX" -x c++ - -latomic -o /dev/null 2>/dev/null; then LINKLIBS=(-latomic); else LINKLIBS=(); fi
RUNS=20
TIMEOUT=120
TASKS=()

while [ $# -gt 0 ]; do
  case "$1" in
    -n) RUNS="$2"; shift 2 ;;
    -t) TIMEOUT="$2"; shift 2 ;;
    --all)
      while IFS= read -r d; do TASKS+=("$(basename "$(dirname "$d")")"); done \
        < <(find "$ROOT"/ch*/ -mindepth 2 -maxdepth 2 -type d -name tests | sort)
      shift ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) TASKS+=("$1"); shift ;;
  esac
done
[ ${#TASKS[@]} -eq 0 ] && { echo "usage: tools/verify.sh <task_name>|--all [-n RUNS] [-t TIMEOUT]"; exit 2; }

# Every task's include/ dir goes on the include path, so a driver that reuses an
# earlier task's header (e.g. threadsafe_stack's driver uses scoped_thread.hpp)
# resolves the same way the CMake target_link_libraries graph does.
INCLUDES=()
while IFS= read -r d; do INCLUDES+=("-I$d"); done \
  < <(find "$ROOT"/ch*/ -mindepth 2 -maxdepth 2 -type d -name include | sort)

NOASLR=(setarch "$(uname -m)" -R)
if ! "${NOASLR[@]}" true 2>/dev/null; then
  echo "note: setarch -R unavailable; running TSAN binaries directly (needs vm.mmap_rnd_bits=28)"
  NOASLR=()
fi

WORK="$(mktemp -d /tmp/verify.XXXXXX)"
overall=0

verify_one() {
  local task="$1"
  local dir
  dir="$(find "$ROOT"/ch*/ -mindepth 1 -maxdepth 1 -type d -name "$task" | head -1)"
  local driver="$dir/tests/${task}_driver.cpp"
  if [ -z "$dir" ] || [ ! -f "$driver" ]; then
    echo "$task: NOT FOUND (expected chXX_*/$task/tests/${task}_driver.cpp)"; return 1
  fi
  local out="$WORK/$task"; mkdir -p "$out"
  local common=(-std=c++17 -g -O1 -pthread "${INCLUDES[@]}")
  local ok=0 tsan_res asan_res link_res keep=""

  # 1. TSAN
  if ! "$CXX" "${common[@]}" -fsanitize=thread "$driver" -o "$out/tsan" ${LINKLIBS[@]+"${LINKLIBS[@]}"} 2>"$out/tsan_build.log"; then
    tsan_res="TSAN BUILD FAILED"; ok=1; keep="$out/tsan_build.log"
  else
    local fail=0 warn=0 i rc
    for ((i=1; i<=RUNS; i++)); do
      timeout "$TIMEOUT" "${NOASLR[@]}" "$out/tsan" >"$out/run.log" 2>&1; rc=$?
      local w; w=$(grep -c "WARNING: ThreadSanitizer" "$out/run.log")
      if [ $rc -ne 0 ] || [ "$w" -gt 0 ]; then
        fail=$((fail+1)); warn=$((warn+w))
        [ -z "$keep" ] && { cp "$out/run.log" "$out/tsan_fail_run$i.log"; keep="$out/tsan_fail_run$i.log"; }
        [ $rc -eq 124 ] && echo "  ($task TSAN run $i timed out after ${TIMEOUT}s -- hang/deadlock?)"
      fi
    done
    if [ $fail -eq 0 ]; then tsan_res="TSAN $RUNS/$RUNS clean"
    else tsan_res="TSAN $((RUNS-fail))/$RUNS clean ($warn race reports)"; ok=1; fi
  fi

  # 2. ASan + UBSan
  if ! "$CXX" "${common[@]}" -fno-omit-frame-pointer -fsanitize=address,undefined \
        -fno-sanitize-recover=undefined "$driver" -o "$out/asan" ${LINKLIBS[@]+"${LINKLIBS[@]}"} 2>"$out/asan_build.log"; then
    asan_res="ASan+UBSan BUILD FAILED"; ok=1; [ -z "$keep" ] && keep="$out/asan_build.log"
  else
    local afail=0 i rc
    for ((i=1; i<=RUNS; i++)); do
      ASAN_OPTIONS=detect_leaks=0 timeout "$TIMEOUT" "$out/asan" >"$out/run.log" 2>&1; rc=$?
      if [ $rc -ne 0 ] || grep -q "runtime error:\|ERROR: AddressSanitizer" "$out/run.log"; then
        afail=$((afail+1))
        [ -z "$keep" ] && { cp "$out/run.log" "$out/asan_fail_run$i.log"; keep="$out/asan_fail_run$i.log"; }
      fi
    done
    if [ $afail -eq 0 ]; then asan_res="ASan+UBSan $RUNS/$RUNS clean"
    else asan_res="ASan+UBSan $((RUNS-afail))/$RUNS clean"; ok=1; fi
  fi

  # 3. Two-TU link check (ODR)
  local hdrs=() h
  while IFS= read -r h; do hdrs+=("$h"); done < <(find "$dir/include" -name '*.hpp' | sort)
  : >"$out/tu_a.cpp"; : >"$out/tu_b.cpp"
  for h in "${hdrs[@]}"; do
    echo "#include \"$h\"" >>"$out/tu_a.cpp"; echo "#include \"$h\"" >>"$out/tu_b.cpp"
  done
  echo "int main() { return 0; }" >>"$out/tu_b.cpp"
  if [ ${#hdrs[@]} -eq 0 ]; then
    link_res="2-TU link: no headers"
  elif "$CXX" -std=c++17 -pthread "${INCLUDES[@]}" "$out/tu_a.cpp" "$out/tu_b.cpp" \
        -o "$out/tu" ${LINKLIBS[@]+"${LINKLIBS[@]}"} 2>"$out/link.log"; then
    link_res="2-TU link OK"
  elif grep -q "multiple definition" "$out/link.log"; then
    local syms; syms=$(grep -o "multiple definition of \`[^']*'" "$out/link.log" | sed "s/multiple definition of \`//; s/'$//" | sort -u | head -5 | paste -sd, -)
    link_res="2-TU link FAILED: ODR multiple definition of: $syms"; ok=1
    [ -z "$keep" ] && keep="$out/link.log"
  else
    link_res="2-TU link inconclusive (non-ODR link error, see $out/link.log)"
  fi

  echo "$task: $tsan_res | $asan_res | $link_res"
  [ -n "$keep" ] && echo "  first failure log: $keep"
  return $ok
}

for t in "${TASKS[@]}"; do
  verify_one "$t" || overall=1
done
[ $overall -eq 0 ] && rm -rf "$WORK"
exit $overall
