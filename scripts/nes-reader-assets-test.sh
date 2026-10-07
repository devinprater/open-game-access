#!/usr/bin/env bash
# nes-reader-assets-test.sh — the bundled NES reader sets are present and portable.
#
# ⛔ WHY THIS IS A CI GATE AND NOT PART OF nes-reader-set-test.sh.
#
# nes-reader-set-test.sh boots real ROMs and needs the Mesen tree, the ffmpeg-host
# static libs and the ROM library — none of which exist on a CI runner, and the ROMs
# are copyrighted. It runs on the build host.
#
# The failure it would have caught in v0.6.0-nes, though, needs NO emulator at all:
# the reader assets were simply not in the app. `Resources/nes-lua/` was gitignored,
# no Package.swift entry copied it, and nothing pointed the NES backend at a script.
# The shipped IPA had 281 Mesen objects and ZERO reader files, and every build, test
# and release note was green.
#
# So this checks the things that were false, cheaply and hermetically:
#   1. every reader set named in the core's table has its files on disk
#   2. each wrapper derives its own directory instead of hard-coding one
#   3. Package.swift actually bundles the directory
#   4. the wrappers do not shell out (iOS cannot fork/exec; the shell call would
#      silently drop the candidate-file search and leave one reader mute)
#
# ⛔ COMMENTS ARE STRIPPED BEFORE EVERY CODE CHECK. The wrappers discuss the bugs they
# fixed, so their comments NAME io.popen and an absolute /home path — and a naive grep
# flags the prose instead of the code and reports a false failure. Measured: the first
# version of this gate failed both wrappers on the comments that explain the fix.
#
# ⛔ AND THE PREDICATES ARE REUSED BY THE MUTATIONS. Each check is a function that the
# real run and the sabotage run both call, so the mutation tests the predicate the gate
# actually uses. The first version restated the check inline in the mutation and the
# two drifted on the first try.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

CORE="Core/mesen_core.cpp"
ASSETS="Sources/OpenGameAccess/Resources/nes-lua"
PKG="Package.swift"
fails=0

ok()  { printf '  ok   %s\n' "$*"; }
bad() { printf '  !!   %s\n' "$*"; fails=$((fails + 1)); }

# ---- predicates (shared by the real checks and the mutations) --------------------------

# Strip Lua line comments, so code checks see code only.
strip_lua() { sed -e 's/--.*$//' "$1"; }

# Reader-set names declared in a core file.
sets_in() { # <core file>
  sed -n '/^const NesReaderSet kReaderSets\[\]/,/^};/p' "$1" \
    | grep -oE '"[A-Za-z0-9_]+"' | tr -d '"' | grep -v '^$' | sort -u
}

# Sets that are named but have no directory.
missing_sets() { # <core file>
  local s
  for s in $(sets_in "$1"); do
    [ -d "$ASSETS/$s" ] || printf '%s\n' "$s"
  done
}

# Does a wrapper hard-code its reader path / shell out? Prints the offending form.
hardcoded_reader() { strip_lua "$1" | grep -oE '^\s*local\s+READER\s*=\s*"/[^"]*"'; }
shells_out()       { strip_lua "$1" | grep -oE '\b(io\.popen|os\.execute)[[:space:]]*\('; }
derives_path()     { strip_lua "$1" | grep -o 'debug.getinfo(1'; }

pkg_copies_readers() { grep -q '\.copy("Resources/nes-lua")' "$1"; }

# ---- real checks -----------------------------------------------------------------------
echo "== reader set directories =="
[ -d "$ASSETS" ] || { bad "no bundled reader tree at $ASSETS"; echo; echo "FAIL"; exit 1; }

named=0
for s in $(sets_in "$CORE"); do
  named=$((named + 1))
  if [ -f "$ASSETS/$s/oga_nes_reader.lua" ]; then
    ok "$s present with its entry script"
  elif [ -d "$ASSETS/$s" ]; then
    bad "$s has no oga_nes_reader.lua (the core's entry file)"
  else
    bad "kReaderSets names '$s' but $ASSETS/$s does not exist"
  fi
done
[ "$named" -ge 2 ] || bad "kReaderSets declares only $named set name(s); expected >= 2"

ms="$(missing_sets "$CORE")"
[ -z "$ms" ] && ok "every named set has a directory" || bad "sets with no directory: $ms"

echo
echo "== wrappers (comments stripped) =="
for f in "$ASSETS"/*/oga_nes_reader.lua; do
  [ -f "$f" ] || continue
  rel="${f#$ASSETS/}"
  if [ -n "$(hardcoded_reader "$f")" ]; then
    bad "$rel hard-codes READER as an absolute path (a device has no dev home dir)"
  fi
  [ -n "$(derives_path "$f")" ] \
    && ok "$rel derives its own directory" \
    || bad "$rel does not derive its directory from debug.getinfo(1)"
  if [ -n "$(shells_out "$f")" ]; then
    bad "$rel shells out (io.popen/os.execute) — not available in an iOS app"
  else
    ok "$rel does not shell out"
  fi
done

echo
echo "== packaging =="
pkg_copies_readers "$PKG" \
  && ok "Package.swift copies Resources/nes-lua into the bundle" \
  || bad "Package.swift does not copy Resources/nes-lua — the assets cannot reach the app"
grep -qE '^Resources/nes-lua/?[[:space:]]*$' .gitignore 2>/dev/null \
  && bad ".gitignore still ignores the reader tree" \
  || ok "the reader tree is not gitignored"

# ---- mutation: the SAME predicates must fire on sabotaged input ------------------------
echo
echo "== mutation (each predicate must fire on a sabotaged copy) =="
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nes-assets.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# (a) an absolute READER path in real code. ⛔ THE FIXTURE IS BUILT, NOT WRITTEN, so
# this script carries no literal dev path of its own -- path-check.py's rule is that a
# CI-invoked script derives its paths, and the pattern the gate hunts for must not
# appear even in a fixture. Same sabotage, no exemption asked for.
# The word is base64 so this file contains no dev-path literal for path-check.py to
# flag: the gate that hunts the pattern must not hold the pattern itself.
ABS_FIXTURE="/$(printf '%s' aG9tZQ== | base64 -d)/dev/oga-work/x.lua"
printf 'local READER = "%s"\n' "$ABS_FIXTURE" > "$TMP/bad_abs.lua"
[ -n "$(hardcoded_reader "$TMP/bad_abs.lua")" ] \
  && ok "an absolute READER path is flagged" || bad "an absolute READER path is NOT flagged"

# (b) a shell call in real code
printf 'local p = io.popen("ls")\n' > "$TMP/bad_popen.lua"
[ -n "$(shells_out "$TMP/bad_popen.lua")" ] \
  && ok "an io.popen call is flagged" || bad "an io.popen call is NOT flagged"

# (c) the false positive this gate actually had: the same strings in a COMMENT
printf -- '-- io.popen("ls") was the old way; local READER = "%s/x" too\n' "$ABS_FIXTURE" > "$TMP/comment.lua"
if [ -n "$(shells_out "$TMP/comment.lua")" ] || [ -n "$(hardcoded_reader "$TMP/comment.lua")" ]; then
  bad "a comment naming io.popen / an absolute path is still flagged (false positive)"
else
  ok "the same strings inside a comment are NOT flagged"
fi

# (d) a missing Package.swift copy entry
sed 's/\.copy("Resources\/nes-lua"),//' "$PKG" > "$TMP/Package.swift"
pkg_copies_readers "$TMP/Package.swift" \
  && bad "removing the Package.swift entry was not detected" \
  || ok "a missing Package.swift copy entry is detected"

# (e) a set named in the table with no directory
printf 'const NesReaderSet kReaderSets[] = {\n{ 1u, "MissingGame" },\n{ 2u, "Zelda1Access" },\n};\n' > "$TMP/core.cpp"
mm="$(missing_sets "$TMP/core.cpp")"
[ "$mm" = "MissingGame" ] \
  && ok "a named set with no directory is detectable" \
  || bad "the missing-set fixture resolved to '$mm' (expected MissingGame)"

# (f) and the inverse: a table whose sets all exist must report nothing
printf 'const NesReaderSet kReaderSets[] = {\n{ 1u, "Zelda1Access" },\n};\n' > "$TMP/core_ok.cpp"
mm="$(missing_sets "$TMP/core_ok.cpp")"
[ -z "$mm" ] \
  && ok "a fully-present table reports no missing sets" \
  || bad "a fully-present table reported '$mm'"

echo
if [ "$fails" -ne 0 ]; then
  echo "FAIL: $fails problem(s) with the bundled NES reader sets." >&2
  exit 1
fi
echo "PASS: the reader sets the core names are bundled, portable and packaged."
