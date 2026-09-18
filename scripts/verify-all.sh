#!/bin/bash
# Final verification: skill integrity, doc state, repo state.
set -eu
R=/home/devin/oga-work

echo "=== skill files ==="
python3 - <<'PY'
import re, os
base = r"C:\Users\Devin Prater\AppData\Local\hermes\skills\software-development\ppsspp-memory-discovery"
# WSL cannot see the Windows path directly; check via the mounted path instead
mnt = "/mnt/c/Users/Devin Prater/AppData/Local/hermes/skills/software-development/ppsspp-memory-discovery"
for name in ["SKILL.md", "references/elf-and-packfile-techniques.md",
             "references/tagteam-phase-and-position.md", "references/visual-novel-readers.md"]:
    p = os.path.join(mnt, name)
    if not os.path.exists(p):
        print("  (from WSL) missing:", name); continue
    t = open(p, encoding="utf-8").read()
    hs = re.findall(r"(?m)^## (.+)$", t)
    dup = [h for h in set(hs) if hs.count(h) > 1]
    print("  %-44s %7d chars %3d heads dupes=%s" % (name, len(t), len(hs), dup if dup else "none"))
PY

echo
echo "=== doc at HEAD ==="
echo "bytes:    $(git -C "$R" show HEAD:docs/reverse-engineering/dissidia-final-fantasy.md | wc -c)"
echo "headings: $(git -C "$R" show HEAD:docs/reverse-engineering/dissidia-final-fantasy.md | grep -c '^## ')"
echo "sections present:"
git -C "$R" show HEAD:docs/reverse-engineering/dissidia-final-fantasy.md | grep -oE '^## 1[7-9]\.' | sed 's/^/  /'

echo
echo "=== repo ==="
echo "remote:   $(git -C "$R" ls-remote origin main | cut -f1)"
echo "local:    $(git -C "$R" rev-parse HEAD)"
echo "unpushed: $(git -C "$R" rev-list --count origin/main..HEAD)"
echo "dirty:    $(git -C "$R" status --short | wc -l) file(s)"
