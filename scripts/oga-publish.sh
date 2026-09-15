#!/usr/bin/env bash
# oga-publish.sh — stage the publish tree and push, then verify the remote actually has
# the new paths.
#
# ⛔ WHY VERIFYING IS PART OF PUBLISHING. scripts/stage-repo.sh copies NAMED paths only
# (an allow-list). A new top-level directory that is not in that list never reaches
# GitHub — but the commit still succeeds and prints "files: 465", so the push looks
# fine. `tools/re/` was silently absent from the remote for exactly this reason. Checking
# the remote after every push is what caught it.
set -uo pipefail

REPO_WIN="/mnt/c/Users/Devin Prater/open-game-access"
EXPECT_PATHS=("tools/re" "reverse-engineering" "docs/reverse-engineering")

cd "$REPO_WIN" || exit 1

echo "== staging =="
bash "$REPO_WIN/wsl.sh" stage-all 2>&1 | tail -4

echo
echo "== committing and pushing =="
bash "$REPO_WIN/wsl.sh" commit-push 2>&1 | tail -5

echo
echo "== verifying the remote actually has the expected paths =="
# ⛔ `gh` IS NOT INSTALLED IN WSL. Running this verification inside WSL makes every
# `gh api` call fail, `grep -c` return 0, and the check report MISSING for paths that are
# present — a verification that lies is worse than no verification. Check for gh first
# and say so rather than reporting a false failure.
if ! command -v gh >/dev/null 2>&1; then
  echo "  !! gh not available here — cannot verify the remote from this shell."
  echo "     Run the verification on the Windows side:"
  echo "       gh api 'repos/devinprater/open-game-access/git/trees/main?recursive=1' \\"
  echo "         --jq '.tree[].path' | grep -c '^tools/re'"
  exit 3
fi

ok=1
for p in "${EXPECT_PATHS[@]}"; do
  n=$(gh api "repos/devinprater/open-game-access/git/trees/main?recursive=1" \
        --jq '.tree[].path' 2>/dev/null | grep -c "^$p" || true)
  if [ "${n:-0}" -gt 0 ]; then
    echo "  ok       $p ($n files)"
  else
    echo "  MISSING  $p  <-- not on the remote; check stage-repo.sh's allow-list"
    ok=0
  fi
done

echo
echo "== game data on the remote (must be 0) =="
bad=$(gh api "repos/devinprater/open-game-access/git/trees/main?recursive=1" \
      --jq '.tree[].path' 2>/dev/null \
      | grep -icE '\.(nds|gba|gbc|gb|nes|smc|sfc|z64|n64|iso|cue|bin|sav|dsv|duc)$' || true)
echo "  game-data files: ${bad:-0}"
[ "${bad:-0}" -eq 0 ] && echo "  ok — no ROMs, BIOS or saves on the remote" \
                      || ok=0

exit $(( 1 - ok ))
