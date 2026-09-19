#!/bin/bash
# commit section 100 + uiroot6/7 scripts.
set -eu
R=/home/devin/oga-work
W="/mnt/c/Users/Devin Prater/open-game-access"
cp -f "$W/docs/reverse-engineering/dissidia-final-fantasy.md" "$R/docs/reverse-engineering/dissidia-final-fantasy.md"
cp -f "$W/scripts/psp-find-uiroot6.py" "$R/scripts/psp-find-uiroot6.py"
cp -f "$W/scripts/psp-find-uiroot7.py" "$R/scripts/psp-find-uiroot7.py"
git -C "$R" add -A
git -C "$R" commit -m "Dissidia: display-judged delivery gate + pause-menu pair blank (s100)" --quiet || echo "nothing to commit"
git -C "$R" push 2>&1 | tail -1
git -C "$R" rev-parse HEAD
git -C "$R" status --short | head -3
echo DONE
