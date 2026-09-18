#!/bin/bash
# Copy the three Ghidra scripts into the git tree and commit+push (javac check already done in MSYS).
set -eu
R=/home/devin/oga-work
GH="/mnt/c/Users/Devin Prater/oga-ghidra-dissidia"

for f in DisScanStringRefs.java DisFindLoaders.java DisFindStrings.java; do
  cp -f "$GH/$f" "$R/scripts/"
done

git -C "$R" add -A scripts/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -m "Dissidia: mark the two Ghidra scripts whose targets use the WRONG ADDRESS SPACE

Third instance of the file-offset/address-space bug class here, found by asking why
DisScanStringRefs reports 0 matches against the CORRECT (DISSIDIA_ELF) project too.

Its TARGETS are RAM-space addresses (0x08B75F68 ...) but the ELF listing uses vaddr space
(0x00371F68 ...); the two differ by the load base 0x08804000, confirmed independently in
section 19. So it searched for addresses that exist at a different number, in BOTH projects,
producing '0 candidate matches' across 783,694 instructions -- which reads as 'the code never
references these strings'.

DisFindLoaders.java has the same defect (0x08B76E94 targets), plus it relied on Ghidra's
reference table which a raw BinaryLoader import does not populate -- two independent reasons
for the same null.

Both annotated in place with SUPERSEDED banners naming the equivalent vaddr, explaining the
null is an artifact, and pointing at DisFindStrings.java -- which searches Ghidra memory for
the string BYTES rather than precomputing an address, so it cannot mismatch the space. That
run found 23 occurrences and 10 functions (section 15).

Both edited files recompiled under javac to confirm nothing broke."
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
