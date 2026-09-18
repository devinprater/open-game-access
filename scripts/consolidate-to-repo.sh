#!/bin/bash
# Consolidate: copy EVERY untracked script and doc from the Windows tree into the git repo (~/oga-work).
#
# WHY: the user's standing directive is "Consolidate into the Git repo. Use that from now on."
# A tree check found 29 scripts and 1 doc present only on the Windows side, including Tag Team
# tooling from earlier sessions. This brings them in.
#
# SAFETY: copies only; never deletes from the Windows tree. Refuses to run if the repo looks wrong.
set -eu
R=/home/devin/oga-work
W="/mnt/c/Users/Devin Prater/open-game-access"

if [ ! -d "$R/.git" ]; then echo "ERROR: $R is not a git repo"; exit 1; fi

added=0
while IFS= read -r f; do
  b=$(basename "$f")
  if ! git -C "$R" ls-files --error-unmatch "scripts/$b" >/dev/null 2>&1; then
    cp -f "$f" "$R/scripts/$b"
    echo "  + scripts/$b"
    added=$((added+1))
  fi
done < <(find "$W/scripts" -maxdepth 1 -type f)

while IFS= read -r f; do
  rel=${f#"$W"/}
  if ! git -C "$R" ls-files --error-unmatch "$rel" >/dev/null 2>&1; then
    mkdir -p "$(dirname "$R/$rel")"
    cp -f "$f" "$R/$rel"
    echo "  + $rel"
    added=$((added+1))
  fi
done < <(find "$W/docs" -type f)

echo
echo "copied $added file(s) into the repo"
git -C "$R" add -A scripts/ docs/
echo "staged: $(git -C "$R" diff --cached --name-only | wc -l) file(s)"
