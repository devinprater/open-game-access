#!/bin/bash
# Commit + push the Dissidia doc/script updates, then verify at HEAD and on the remote.
# Written as a file because `cd X && git ...` in a compound wsl.exe -lc call does not persist cwd.
set -eu
R=/home/devin/oga-work
MSG="/mnt/c/Users/Devin Prater/AppData/Local/Temp/commitmsg17.txt"
WD="/mnt/c/Users/Devin Prater/open-game-access/scripts"
GH="/mnt/c/Users/Devin Prater/oga-ghidra-dissidia"

for f in append-section17.sh DisDecompile.java DisDecompile2.java DisDecompile3.java; do
  if [ -f "$WD/$f" ]; then cp -f "$WD/$f" "$R/scripts/";
  elif [ -f "$GH/$f" ]; then cp -f "$GH/$f" "$R/scripts/"; fi
done

git -C "$R" add -A docs/ scripts/
git -C "$R" -c user.email=devin@localhost -c user.name=devin commit -q -F "$MSG"
git -C "$R" push -q origin main

echo "remote: $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:  $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
echo "doc at HEAD: $(git -C "$R" show HEAD:docs/reverse-engineering/dissidia-final-fantasy.md | wc -c) bytes"
echo "sections at HEAD: $(git -C "$R" show HEAD:docs/reverse-engineering/dissidia-final-fantasy.md | grep -c '^## ')"
