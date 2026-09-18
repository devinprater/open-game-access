#!/bin/bash
# Is the git repo (~/oga-work) the canonical tree, and does it hold everything?
# NOTE: every path MUST be quoted -- "Devin Prater" contains a space, and an unquoted
# glob/loop splits on it, producing a flood of bogus "untracked" lines.
set -eu
R=/home/devin/oga-work
W="/mnt/c/Users/Devin Prater/open-game-access"

echo "=== git tree (~/oga-work, the canonical repo) ==="
echo "tracked files total : $(git -C "$R" ls-files | wc -l)"
echo "tracked scripts     : $(git -C "$R" ls-files scripts/ | wc -l)"
echo "tracked docs        : $(git -C "$R" ls-files docs/ | wc -l)"

echo
echo "=== Windows tree ==="
echo "scripts on disk     : $(find "$W/scripts" -maxdepth 1 -type f | wc -l)"
echo "docs on disk        : $(find "$W/docs" -type f | wc -l)"

echo
echo "=== scripts on Windows but NOT tracked in git ==="
n=0
while IFS= read -r f; do
  b=$(basename "$f")
  if ! git -C "$R" ls-files --error-unmatch "scripts/$b" >/dev/null 2>&1; then
    echo "  UNTRACKED: $b"
    n=$((n+1))
  fi
done < <(find "$W/scripts" -maxdepth 1 -type f)
echo "  total: $n"

echo
echo "=== docs on Windows but NOT tracked in git ==="
m=0
while IFS= read -r f; do
  rel=${f#"$W"/}
  if ! git -C "$R" ls-files --error-unmatch "$rel" >/dev/null 2>&1; then
    echo "  UNTRACKED: $rel"
    m=$((m+1))
  fi
done < <(find "$W/docs" -type f)
echo "  total: $m"
