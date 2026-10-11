#!/usr/bin/env python3
"""test-strength-check.py -- no adapter host test may quietly lose its checks.

WHY. The rest of CI proves the ADAPTERS behave. Nothing proves the TESTS still
check anything: every suite here passes just as green with twenty checks deleted,
because the only thing anyone reads is the exit code. A gate that cannot notice its
own weakening is the failure mode the 500B-token decompilation project hit -- agents
edited the comparison script until their own functions passed, and the fix was to
stop trusting a file that lives in the same tree as the thing it judges. See
docs/research/agent-harness-lessons.md.

WHAT IT DOES. Counts the assertion call sites in each file listed in
scripts/test-strength-baseline.txt and fails when a file has fewer than its recorded
floor. To lower a floor you edit the baseline file in the same commit, which is
visible in review -- a deliberate act, which is the whole point.

WHAT IT DOES NOT DO. It catches DELETED checks, not REWRITTEN ones: a check turned
into `if (x) ok() else bad()` still counts. It is a floor against quiet erosion, not
a proof of coverage. The stronger version -- hashing the harness against a value that
is not in the tree -- is written up in the doc above and needs a repo secret.

Usage:
  python3 scripts/test-strength-check.py                 # the gate
  python3 scripts/test-strength-check.py --update        # rewrite the baseline
  python3 scripts/test-strength-check.py --self-test     # prove this gate CAN fail
"""
import argparse
import pathlib
import re
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "scripts" / "test-strength-baseline.txt"

# Every way a host test says "this must hold". The suites are not consistent: the
# announce/systems suites use CHECK/CHECKSTR/ok as macros or lambdas, gba_adapter_test
# uses lower-case check()/check_str() helpers, dissidia uses CHECK and ok. All of them
# count. bad() is deliberately NOT here: it is the failing half of the same site, so
# counting both would double every if/else.
SITE = re.compile(r"\b(?:check(?:str|_str)?|ok)\s*\(", re.IGNORECASE)

HEADER = """# test-strength-baseline.txt -- the assertion floor for every adapter host test.
#
# <path>  <minimum assertion call sites>
#
# scripts/test-strength-check.py fails when a file drops below its number. Lowering a
# number is how you delete a check on purpose; it belongs in the same commit, so the
# reviewer sees it. Do not lower one to make a red build green.
#
# Rewrite with:  python3 scripts/test-strength-check.py --update
"""


def count(path: pathlib.Path) -> int:
    return len(SITE.findall(path.read_text(encoding="utf-8", errors="replace")))


def read_baseline(path: pathlib.Path) -> dict[str, int]:
    out: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 2 or not parts[1].lstrip("-").isdigit():
            sys.exit(f"!! baseline line is not '<path> <count>': {line!r}")
        out[parts[0]] = int(parts[1])
    return out


def targets() -> list[pathlib.Path]:
    """Every Core host test, plus the two suites that live beside them."""
    files = sorted((ROOT / "Core").glob("*_test.cpp"))
    return [f for f in files if f.is_file()]


def check(baseline_path: pathlib.Path) -> int:
    base = read_baseline(baseline_path)
    problems: list[str] = []
    rows: list[tuple[str, int, int]] = []
    for f in targets():
        rel = f.relative_to(ROOT).as_posix()
        n = count(f)
        floor = base.get(rel)
        rows.append((rel, n, -1 if floor is None else floor))
        if floor is None:
            problems.append(f"{rel}: not in the baseline ({n} sites). Baseline it: --update")
        elif n < floor:
            problems.append(
                f"{rel}: {n} assertion sites, baseline says {floor}. "
                f"{floor - n} check(s) went missing. If that was on purpose, lower the "
                f"number in {baseline_path.name} in THIS commit."
            )
    for rel, floor in base.items():
        if not (ROOT / rel).exists():
            problems.append(f"{rel}: in the baseline but the file is gone. Delete the row or restore it.")

    for rel, n, floor in sorted(rows):
        mark = "ok " if floor >= 0 and n >= floor else "!! "
        shown = "unbaselined" if floor < 0 else str(floor)
        print(f"  {mark}{n:4d} sites (floor {shown:>11})  {rel}")
    if problems:
        print()
        for p in problems:
            print("  FAIL " + p)
        return 1
    print(f"\nPASS: {len(rows)} host test(s) still carry at least their recorded checks.")
    return 0


def update() -> int:
    rows = [(f.relative_to(ROOT).as_posix(), count(f)) for f in targets()]
    width = max(len(r[0]) for r in rows) if rows else 0
    body = "".join(f"{rel:<{width}}  {n}\n" for rel, n in rows)
    BASELINE.write_text(HEADER + body, encoding="utf-8")
    print(f"wrote {BASELINE.relative_to(ROOT)} with {len(rows)} rows")
    return 0


def self_test() -> int:
    """A gate that only ever passes is not a gate: prove this one can fail."""
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="oga-strength-"))
    try:
        good = tmp / "baseline-good.txt"
        good.write_text(HEADER + "".join(f"{f.relative_to(ROOT).as_posix()}  {count(f)}\n" for f in targets()))
        if check(good) != 0:
            print("!! self-test: the honest baseline does not pass", file=sys.stderr)
            return 1

        bad = tmp / "baseline-bad.txt"
        first = targets()[0]
        bad.write_text(
            HEADER
            + f"{first.relative_to(ROOT).as_posix()}  {count(first) + 5}\n"
        )
        print("\n-- self-test: one floor raised above the file's real count, expect FAIL --")
        if check(bad) == 0:
            print("!! self-test: a floor raised above the truth still passed", file=sys.stderr)
            return 1
        print("\nPASS: the gate fails when a file stops carrying its checks, and passes when it does.")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true", help="rewrite the baseline from the tree")
    ap.add_argument("--self-test", action="store_true", help="prove the gate can fail")
    args = ap.parse_args()
    if args.update:
        return update()
    if args.self_test:
        return self_test()
    return check(BASELINE)


if __name__ == "__main__":
    sys.exit(main())
