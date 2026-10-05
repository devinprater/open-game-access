#!/usr/bin/env python3
"""path-check.py — a CI-reachable script must not hardcode a developer's absolute path.

⛔ THE BUG THIS EXISTS FOR, TWICE OVER. scripts/check-shell-syntax.sh carries a comment
describing this exact failure ("died in CI with `cd: /home/devin/oga-work: No such file or
directory`") and FOUR other tracked scripts still hardcoded it — one of them called BY CI.
Then the brand-new swift/ABI checks repeated it and failed their first CI run outright.
A comment did not prevent the recurrence; neither did fixing one instance. So it is checked
mechanically now.

⛔ SCOPE, AND WHY IT IS NARROW ON PURPOSE. The first version of this check flagged every
tracked script, which produced 44 hits — and most were LOCAL-ONLY diagnostics (ROM sweeps,
screenshot tools) that name a ROM directory outside the repo. ROMs must live outside the
repo by this project's policy, so those scripts can ONLY name them absolutely; flagging them
is noise, and a gate that cries wolf is one people learn to ignore. So the rule is precise:
**a script CI invokes must derive its own paths.** A local diagnostic may be local.

The CI-reachable set is computed from the workflows, one level of indirection deep (a CI
script may call another via `$ROOT/scripts/X`).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def ci_reachable():
    direct = set()
    for f in sorted(ROOT.glob(".github/workflows/*.yml")):
        t = f.read_text(encoding="utf-8", errors="replace")
        direct |= set(re.findall(r'(?:bash|python3|sh)\s+(scripts/[\w.-]+)', t))
        direct |= set(re.findall(r'\./(scripts/[\w.-]+)', t))
    indirect = set()
    for rel in list(direct):
        p = ROOT / rel
        if not p.exists():
            continue
        t = p.read_text(encoding="utf-8", errors="replace")
        indirect |= set(re.findall(r'\$[A-Za-z_]+/scripts/([\w.-]+)', t))
    reach = direct | {"scripts/" + pathlib.Path(x).name for x in indirect}
    return {r for r in reach if (ROOT / r).exists()}


def live_dev_paths(path):
    # ⛔ THIS CHECKER IS EXEMPT FROM ITSELF, and the reason is not convenience: its job is to
    # hold the literal patterns it searches for ('/home/[a-z]', '/mnt/c/Users/'), and its
    # docstring quotes the real path that failed in CI. A pattern table necessarily contains
    # its patterns. Skipping the file beats weakening the patterns for everything else.
    if path.name in ("path-check.py", "path-check-test.sh"):
        return []

    t = path.read_text(encoding="utf-8", errors="replace")
    hits = []
    in_docstring = False
    for i, line in enumerate(t.splitlines(), 1):
        s = line.strip()
        # a triple-quoted block is prose (the recorded lesson), not executable lines
        if s.count('"""') % 2 == 1:
            in_docstring = not in_docstring
            continue
        if in_docstring:
            continue
        if s.startswith("#"):
            continue           # the lesson must be allowed to stay written down
        if "re.compile(" in line or "re.search(" in line or "PATTERNS" in line:
            continue           # a pattern definition is not a live path
        if re.search(r'/home/[a-z]|/Users/[A-Za-z]|/mnt/c/Users/', line):
            hits.append((i, s[:100]))
    return hits


def main():
    reach = ci_reachable()
    bad = {r: live_dev_paths(ROOT / r) for r in sorted(reach)}
    bad = {r: h for r, h in bad.items() if h}

    print("path-check: %d CI-reachable script(s) scanned" % len(reach))
    if not bad:
        print("PASS: every CI-reachable script derives its paths")
        return 0
    print("!! a CI-invoked script hardcodes a developer's absolute path:")
    for rel, hits in bad.items():
        for i, line in hits:
            print("   %s:%d  %s" % (rel, i, line))
    print()
    print("A gate on a dev machine and a gate in CI are different programs unless paths")
    print('are derived. Replace with: R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"')
    return 1


if __name__ == "__main__":
    sys.exit(main())
