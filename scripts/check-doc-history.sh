#!/bin/bash
# Inspect the doc's size across recent commits so a clobbered version can be located.
set -u
cd /home/devin/oga-work || exit 1
for c in $(git log --format=%h -10); do
  sz=$(git show "$c:docs/reverse-engineering/dissidia-final-fantasy.md" 2>/dev/null | wc -c)
  n=$(git show "$c:docs/reverse-engineering/dissidia-final-fantasy.md" 2>/dev/null | grep -c '^## ' || true)
  subj=$(git log -1 --format=%s "$c" | cut -c1-46)
  printf '  %s  %8s bytes  %3s headings  %s\n' "$c" "$sz" "$n" "$subj"
done
