#!/bin/bash
# Sync the maintenance scripts to the WSL git tree and commit+push, then verify.
set -eu
R=/home/devin/oga-work
WD="/mnt/c/Users/Devin Prater/open-game-access"

# removed one-off scripts should be dropped from the repo too
git -C "$R" rm -q --ignore-unmatch \
  scripts/restore-doc.sh scripts/append-sections.sh scripts/append-section17.sh \
  scripts/commit-section18.sh scripts/check-dupe-sections.sh scripts/prep-decompile4.sh 2>/dev/null || true

for f in sync-dissidia.sh sync-back.py dedupe-reference.py commit-push-verify.sh \
         final-state.sh check-doc-history.sh; do
  [ -f "$WD/scripts/$f" ] && cp -f "$WD/scripts/$f" "$R/scripts/"
done

git -C "$R" add -A scripts/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -m "repo hygiene: dedupe corrupted skill reference, drop one-off recovery scripts

- references/elf-and-packfile-techniques.md had been silently CORRUPTED by an offload that
  appended content already present: 94,234 chars, 95 heading blocks, 47 duplicated (every
  rule twice). Deduped to 47,487 chars / 47 blocks, keeping first occurrences only where the
  bodies matched; all four skill files now verify with zero duplicate headings.
- corrected a factual error in Rule 79: it claimed Ghidra 'swallows the javac output'. The
  log shows Ghidra DOES print the compiler diagnostic (location:/skipping:/error:), just
  buried in INFO output. Rule rewritten and marked as a correction.
- fixed a stale header ('rules 94-103' while the file held rules 76-125) and a stale
  cross-reference in SKILL.md that pointed at the wrong rule number.
- added scripts/dedupe-reference.py (keeps a .bak, refuses to write under 10 KB).
- removed six single-use incident-recovery scripts whose job is done: restore-doc.sh,
  append-sections.sh, append-section17.sh, commit-section18.sh, check-dupe-sections.sh,
  prep-decompile4.sh. Kept the reusable ones (sync-dissidia.sh, sync-back.py,
  dedupe-reference.py, commit-push-verify.sh, final-state.sh, check-doc-history.sh).

New skill rules 97 (an offload can duplicate content silently -- verify every reference
after a split) and 98 (correct a tool's claim when a log falsifies it)."
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
echo "clean: $(git -C "$R" status --short | wc -l) modified file(s)"
