#!/usr/bin/env python3
"""grep-reader-api.py — enumerate the exact host-API calls the Pokémon Access GB/GBA
readers make, so the mGBA shim implements the real surface rather than a guessed one.

⛔ WHY ENUMERATE RATHER THAN GUESS. The shim's job is to satisfy these 166 files. Writing
a shim from the BizHawk documentation would produce a shim for BizHawk-in-general, not for
what these scripts actually touch — and an unimplemented call surfaces as a runtime error
deep inside a reader, which is far harder to diagnose than a missing entry in this list.

Run:  grep-reader-api.py <reader-dir>
"""
import collections
import pathlib
import re
import sys

# BizHawk / emulator-host namespaces the readers may use.
NS = ["emu", "memory", "joypad", "console", "savestate", "movie", "mainmemory",
      "client", "event", "forms", "gui", "input", "gameinfo", "lsnes"]

CALL = re.compile(r"\b(" + "|".join(NS) + r")\s*[.:]\s*([A-Za-z_][A-Za-z0-9_]*)")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    root = pathlib.Path(sys.argv[1])
    files = sorted(root.rglob("*.lua"))
    if not files:
        raise SystemExit(f"!! no .lua files under {root}")

    calls = collections.Counter()
    where = collections.defaultdict(set)
    total_bytes = 0

    for f in files:
        try:
            text = f.read_text(encoding="utf-8", errors="replace")
        except Exception:                                        # noqa: BLE001
            continue
        total_bytes += len(text)
        # Strip long strings/comments crudely so commented-out code does not inflate
        # the surface — a shim for a commented-out call is wasted work.
        text = re.sub(r"--\[\[.*?\]\]", " ", text, flags=re.S)
        text = re.sub(r"--[^\n]*", " ", text)
        for m in CALL.finditer(text):
            key = f"{m.group(1)}.{m.group(2)}"
            calls[key] += 1
            where[key].add(str(f.relative_to(root)))

    print(f"reader dir : {root}")
    print(f"lua files  : {len(files)}")
    print(f"total bytes: {total_bytes:,}")
    print(f"distinct host API calls: {len(calls)}\n")

    by_ns = collections.defaultdict(list)
    for k, n in calls.items():
        by_ns[k.split(".")[0]].append((k, n))

    for ns in NS:
        if ns not in by_ns:
            continue
        items = sorted(by_ns[ns], key=lambda x: -x[1])
        print(f"===== {ns}. ({len(items)} distinct) =====")
        for k, n in items:
            sample = sorted(where[k])[:2]
            print(f"  {k:<34} {n:>5} calls   e.g. {', '.join(sample)}")
        print()

    # The shim surface, as a machine-readable list for the compat layer.
    out = pathlib.Path(__file__).resolve().parent / "reader-api-surface.txt"
    with out.open("w", encoding="utf-8") as fh:
        fh.write(f"# host API calls used by {len(files)} reader files ({total_bytes:,} bytes)\n")
        for k, n in sorted(calls.items(), key=lambda x: (-x[1], x[0])):
            fh.write(f"{k}\t{n}\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
