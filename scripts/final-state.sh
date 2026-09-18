#!/bin/bash
# Final state check: is everything committed and pushed, and is the working tree clean?
set -eu
R=/home/devin/oga-work
W=/mnt/c/Users/Devin%20Prater/open-game-access

echo "=== WSL git tree ==="
echo "remote:   $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:    $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
echo "dirty files:"
git -C "$R" status --short | head -10
echo "(none above = clean)"

echo
echo "=== docs at HEAD on the remote ==="
git -C "$R" ls-tree --name-only origin/main docs/reverse-engineering/
echo
echo "=== dissidia doc size + sections at HEAD ==="
git -C "$R" show origin/main:docs/reverse-engineering/dissidia-final-fantasy.md | wc -c
git -C "$R" show origin/main:docs/reverse-engineering/dissidia-final-fantasy.md | grep -c '^## '

echo
echo "=== recent commits ==="
git -C "$R" log --oneline -6
