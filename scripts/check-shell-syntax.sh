#!/usr/bin/env bash
# check-shell-syntax.sh — every TRACKED shell script must parse.
#
# ⛔ WHY: scripts/bootstrap-deps.sh sat as a syntax error for a day (commit 248a4d5
# dropped the closing quote on PPSSPP_REPO=...), and NOTHING local noticed -- no
# verification script runs bootstrap-deps.sh. The iOS toolchain was already bootstrapped
# so the release built fine; CI found it on the release tag, which is the worst possible
# place to find it. This is the cheap gate that would have caught it in seconds.
#
# bash -n is a PARSE check: it catches unterminated quotes, missing fi/done, and
# nothing else. That is exactly the class of error that survives every other test here.
# filename because $f was dropped by an unterminated-array artifact; this one is
# explicit and reports a real verdict.)
cd /home/devin/oga-work || exit 1
bad=0
n=0
while IFS= read -r f; do
  n=$((n + 1))
  if ! bash -n "$f" 2>/tmp/serr.txt; then
    echo "  BROKEN: $f"
    sed 's/^/      /' /tmp/serr.txt
    bad=$((bad + 1))
  fi
done < <(git ls-files '*.sh' | grep -v '^third-party/' | grep -v '^Vendor/')
echo "  checked=$n broken=$bad"
[ "$bad" -eq 0 ] && echo "PASS: every tracked shell script parses" || echo "FAIL"
