#!/usr/bin/env bash
# gb-gate-mutations.sh — prove the Game Boy audio/adapter gate can actually FAIL.
#
# ⛔ WHY THIS IS COMMITTED. Two earlier gates in this repo PASSED on the exact mutation they
# were written to catch, so "the gate is green" is not evidence by itself. This script
# re-introduces each bug the gate claims to protect and asserts the gate exits non-zero.
#
# Three bugs:
#   M1  kGbaOps's read_audio slot back to NULL        -> "returned 0 frames -- the GB/GBC/GBA path is silent"
#   M2  opts.volume back to 0 (what memset left it)   -> "the stream is silence (peak 0, ...)"
#   M3  the console gate removed from gba_attach      -> "no refusal: the adapter attached to a .gbc"
#
# ⛔ THE BACKUPS GO UNDER $HOME, NEVER /tmp. /tmp is WIPED between runs in this environment, so a
# backup there fails at restore time and leaves the tree mutated -- measured, and it left the
# clean-tree check reporting FAIL for the wrong reason.
#
# ⛔ IT RUNS FROM A CLEAN TREE AND RESTORES VIA `git checkout` AS WELL AS THE BACKUPS, so a
# failed restore cannot silently become the next run's starting point.
set -u
R="${OGA_ROOT:-$HOME/oga-work}"
cd "$R" || exit 1

if [ -n "$(git status --porcelain)" ]; then
  echo "!! the tree is not clean; refusing to mutate it"
  git status --short | head -5
  exit 2
fi

CORE="$R/Core/oga_core.cpp"; GBA="$R/Core/gba_core.cpp"; GAD="$R/Core/gba_adapter.cpp"
BAK="$HOME/fe/gate-mut-backup"; rm -rf "$BAK"; mkdir -p "$BAK"
cp "$CORE" "$BAK/oga_core.cpp"; cp "$GBA" "$BAK/gba_core.cpp"; cp "$GAD" "$BAK/gba_adapter.cpp"

restore() {
  git checkout -- Core/oga_core.cpp Core/gba_core.cpp Core/gba_adapter.cpp 2>/dev/null
  echo "   (restored via git checkout)"
}
trap restore EXIT

fails=0

check() {          # check <name> <expected-FAIL-message-pattern>
  local name="$1" pat="$2"
  bash scripts/gb-audio-and-adapter-test.sh > "$HOME/fe/gb-mut.log" 2>&1
  local rc=$?
  # ⛔ A SKIP IS NOT A PASS. The gate exits 0 WITHOUT RUNNING when the mGBA tree, the ROM
  # library or the host objects are absent -- on a machine without them every mutation would
  # look like "the gate did not fail", which is the opposite of the truth. Detect it and stop.
  if grep -q "^SKIP" "$HOME/fe/gb-mut.log"; then
    echo "   SKIP: the gate itself skipped, so this machine cannot prove anything:"
    grep "^SKIP" "$HOME/fe/gb-mut.log" | head -2
    return 2
  fi
  if [ "$rc" -ne 0 ] && grep -qE "$pat" "$HOME/fe/gb-mut.log"; then
    echo "  ok   $name: the gate FAILED as required"
  else
    echo "  BAD  $name: gate exit=$rc, pattern '$pat' not matched"
    grep -E "  FAIL" "$HOME/fe/gb-mut.log" | head -3
    fails=$((fails + 1))
  fi
}

echo "===== M1: kGbaOps's read_audio slot back to NULL"
python3 -c "
import re,sys
p='$CORE'; s=open(p,encoding='utf-8').read()
s2,n=re.subn(r'^\s*GbaReadAudio,', '    NULL,', s, count=1, flags=re.M)
assert n==1, 'anchor missed'; open(p,'w',encoding='utf-8').write(s2)"
check "read_audio NULL" "returned 0 frames" || exit 0
git checkout -- Core/oga_core.cpp

echo
echo "===== M2: opts.volume back to 0 (what memset left it at)"
python3 -c "
import re
p='$GBA'; s=open(p,encoding='utf-8').read()
s2,n=re.subn(r'opts\.volume = 0x100;', 'opts.volume = 0;', s, count=1)
assert n==1, 'anchor missed'; open(p,'w',encoding='utf-8').write(s2)"
check "opts.volume = 0" "the stream is silence" || exit 0
git checkout -- Core/gba_core.cpp

echo
echo "===== M3: the console gate removed from gba_attach"
python3 -c "
import re
p='$GAD'; s=open(p,encoding='utf-8').read()
s2,n=re.subn(r'if \(g_platform == 1[^\n]*\) \{', 'if (false) {', s, count=1)
assert n==1, 'anchor missed'; open(p,'w',encoding='utf-8').write(s2)"
check "console gate removed" "no refusal" || exit 0
git checkout -- Core/gba_adapter.cpp

echo
echo "===== restored: the clean tree must PASS"
bash scripts/gb-audio-and-adapter-test.sh > "$HOME/fe/gb-clean.log" 2>&1
rc=$?
echo "   gate exit=$rc"
[ "$rc" -eq 0 ] || fails=$((fails + 1))

echo
if [ -n "$(git status --porcelain)" ]; then
  echo "!! THE TREE IS DIRTY AFTER THE RUN -- a restore failed"
  git status --short | head -5
  fails=$((fails + 1))
fi

if [ "$fails" -eq 0 ]; then
  echo "PASS: all three bugs make the gate fail, and the clean tree passes."
else
  echo "FAIL: $fails check(s) did not behave."
fi
exit "$fails"
