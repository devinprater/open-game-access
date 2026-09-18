#!/bin/bash
# Append section 20 (correction: DAT_00397770 is not the menu manager), commit+push, verify.
set -eu
R=/home/devin/oga-work
DOC="$R/docs/reverse-engineering/dissidia-final-fantasy.md"
SRC="/mnt/c/Users/Devin Prater/AppData/Local/Temp/section20.md"
WIN="/mnt/c/Users/Devin Prater/open-game-access/docs/reverse-engineering/dissidia-final-fantasy.md"

before=$(wc -c < "$DOC")
cat "$SRC" >> "$DOC"
after=$(wc -c < "$DOC")
if [ "$after" -lt "$before" ] || [ "$after" -lt 20000 ]; then
  echo "ERROR: doc guard tripped ($before -> $after); aborting"; exit 1
fi
cp -f "$DOC" "$WIN"
echo "doc: $before -> $after bytes, $(grep -c '^## ' "$DOC") headings"

git -C "$R" add -A docs/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -m "Dissidia: section 20 -- CORRECTION, DAT_00397770 is NOT the menu manager

Section 17 claimed the menu manager is a static struct at DAT_00397770. Reading the live
game (now possible thanks to the confirmed load base) proves that wrong.

DAT_00397770 is a CHAPTER/STAGE NAME TABLE: reading RAM 0x08B9B770 gives one00, two00,
thr00, for00, fiv00, six00, sev00, eht00, nin00, ten00, org50, org00, one01_easy,
two01_easy -- stride 36 bytes (0x24). The fields section 17 called 'listA tail +0x2c' and
'listA count +0x30' are literally the characters 'two0' and '0' of the next entry.

The mistake: FUN_00248dd0(DAT_00397770, param_1) -- DAT_00397770 is the ARGUMENT, param_1 is
the OBJECT. The dispatcher source made this visible all along: FUN_0024932c(int param_1)
reads its lists from param_1+0x28 etc, not from DAT_00397770. So the whole +0x28/+0x2c/+0x30
field map was built from the wrong object.

The real manager is reached via the singleton: FUN_00103c60(p) = *(u32*)(p + 0x2000), called
with DAT_00392cd8. Live, DAT_00392cd8 = 0x08C5DC80, a genuine allocated structure with an
internal pointer network (records clustered 0x08C5DC80-0x08C5FC80, a heap region). That is
consistent with section 16's param_1[8] = -1 sentinel being a field of an ALLOCATED struct
(the Rule 65 situation) -- still the live hypothesis.

Lesson recorded: before building a field map from a global, check HOW it is used in the call
-- argument vs object. A live byte-dump caught this immediately, which is the argument for
reading a global's live value before theorising about its layout."
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
