#!/bin/bash
# Append section 19 (load base derived + confirmed), sync to Windows, commit+push, verify.
set -eu
R=/home/devin/oga-work
DOC="$R/docs/reverse-engineering/dissidia-final-fantasy.md"
SRC="/mnt/c/Users/Devin Prater/AppData/Local/Temp/section19.md"
WIN="/mnt/c/Users/Devin Prater/open-game-access/docs/reverse-engineering/dissidia-final-fantasy.md"

before=$(wc -c < "$DOC")
cat "$SRC" >> "$DOC"
after=$(wc -c < "$DOC")
if [ "$after" -lt "$before" ] || [ "$after" -lt 20000 ]; then
  echo "ERROR: doc guard tripped ($before -> $after); aborting"; exit 1
fi
cp -f "$DOC" "$WIN"
echo "doc: $before -> $after bytes, $(grep -c '^## ' "$DOC") headings"

git -C "$R" add -A docs/ scripts/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -m "Dissidia: section 19 -- load base DERIVED and CONFIRMED as 0x08804000

Every section since 7 carried the caveat that the load base was an unverified placeholder.
Resolved by two independent measurements of the same bytes:
  DISSIDIA_ELF (ElfLoader) puts 'MENU MANAGER' at Ghidra 0x0037F7FC
  a live RAM scan put the same string at         RAM    0x08B837FC
  difference = 0x08804000 = the load base
Importing AS AN ELF is what makes this a single subtraction (Ghidra addr == vaddr); the
superseded BinaryLoader import added a 0x74 skew that made even the correct base look wrong.

Confirmed against the live game with FOUR strings, each computed as base + ghidra_addr and
read back:
  0x0037F7FC MENU MANAGER                -> 0x08B837FC  FOUND
  0x00372E20 pause_help.bin              -> 0x08B76E20  FOUND
  0x00372E60 MENU_MANAGER::ExecuteUpdate -> 0x08B8380C  FOUND
  0x003788E4 item_help.bin               -> 0x08B7C8E4  FOUND

Conversion rule for this build: RAM = 0x08804000 + ghidra_address.

This unblocks the cursor step: the menu manager is a STATIC struct (section 17), so
DAT_00397770 -> RAM 0x08B9B770 is now directly readable, with its item lists at +0x28 head,
+0x2c tail, +0x30 count (+0x34/+0x38/+0x3c for the second list). No scanning needed.
Caveat kept: confirmed for ULUS10437 v1.00 only -- never carry a base across games."
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
