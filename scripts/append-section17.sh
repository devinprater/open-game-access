#!/bin/bash
# Append section 17 to the Dissidia doc (WSL git tree), sync it back to Windows, and verify.
set -eu
DOC=/home/devin/oga-work/docs/reverse-engineering/dissidia-final-fantasy.md
SRC="/mnt/c/Users/Devin Prater/AppData/Local/Temp/section17.md"
WIN="/mnt/c/Users/Devin Prater/open-game-access/docs/reverse-engineering/dissidia-final-fantasy.md"

before=$(wc -c < "$DOC")
cat "$SRC" >> "$DOC"
after=$(wc -c < "$DOC")

# GUARD: refuse to keep a doc that lost content
if [ "$after" -lt "$before" ]; then
  echo "ERROR: doc shrank ($before -> $after); aborting"
  exit 1
fi
if [ "$after" -lt 20000 ]; then
  echo "ERROR: doc is implausibly small ($after bytes); aborting"
  exit 1
fi

cp -f "$DOC" "$WIN"

echo "doc: $before -> $after bytes"
echo "headings: $(grep -c '^## ' "$DOC")"
grep -E '^## 1[5-7]\.' "$DOC" | head -4
