#!/bin/bash
# Sync the Dissidia scripts + doc from the Windows tree into the WSL git tree.
set -u
SRC="/mnt/c/Users/Devin Prater/open-game-access"
DST="/home/devin/oga-work"

mkdir -p "$DST/scripts" "$DST/docs/reverse-engineering"

n=0
for f in "$SRC"/scripts/psp-iso-ls.py \
         "$SRC"/scripts/psp-find-text.py \
         "$SRC"/scripts/psp-ram-utf16.py \
         "$SRC"/scripts/psp-dissidia-package.py \
         "$SRC"/scripts/psp-dissidia-index.py \
         "$SRC"/scripts/psp-dissidia-unpack.py \
         "$SRC"/scripts/psp-dissidia-mpk.py \
         "$SRC"/scripts/psp-dissidia-survey.py \
         "$SRC"/scripts/psp-dissidia-names.py \
         "$SRC"/scripts/psp-dissidia-arc.py \
         "$SRC"/scripts/psp-dissidia-decode.py \
         "$SRC"/scripts/psp-dissidia-cipher.py \
         "$SRC"/scripts/psp-dissidia-textsolve.py \
         "$SRC"/scripts/psp-dissidia-planes.py \
         "$SRC"/scripts/psp-dissidia-seqtext.py \
         "$SRC"/scripts/psp-dissidia-indexclass.py \
         "$SRC"/scripts/psp-dissidia-fontscan.py \
         "$SRC"/scripts/psp-dissidia-ramtext.py \
         "$SRC"/scripts/psp-ppsspp-client.py ; do
  if [ -f "$f" ]; then
    cp -f "$f" "$DST/scripts/" && n=$((n+1))
  else
    echo "MISSING: $f"
  fi
done

cp -f "$SRC/docs/reverse-engineering/dissidia-final-fantasy.md" "$DST/docs/reverse-engineering/"

echo "copied $n scripts"
ls "$DST/scripts" | grep -c dissidia
ls "$DST/scripts" | grep -E 'psp-iso-ls|psp-find-text|psp-ram-utf16'
