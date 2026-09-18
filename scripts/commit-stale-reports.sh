#!/bin/bash
# Sync mark-stale-reports.py and commit+push, then verify.
set -eu
R=/home/devin/oga-work
WD="/mnt/c/Users/Devin Prater/open-game-access"

cp -f "$WD/scripts/mark-stale-reports.py" "$R/scripts/"

git -C "$R" add -A scripts/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -m "Dissidia: flag the superseded Ghidra reports so their 0-hit results cannot be misread

Two Ghidra projects exist: DISSIDIA (BinaryLoader at base 0x08804000 -- WRONG, addresses
skewed 0x74) and DISSIDIA_ELF (ElfLoader -- correct). Reports written by the wrong project
were still on disk looking authoritative, and stringrefs-report.txt literally reads
'scanned 783694 instructions, 0 candidate match(es)'. A future session could take that as
'the game never references these strings', which is FALSE -- the corrected run found 23
hits and decompiled 10 functions.

Annotated both stale files in place with a leading banner explaining the skew and pointing
at strings-report.txt. Kept rather than deleted: they are evidence of the mistake and of
how convincing a wrong null looks.

New script scripts/mark-stale-reports.py does this idempotently (skips already-marked files).

Verified: 5 CURRENT reports, 2 SUPERSEDED and clearly labelled."
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
