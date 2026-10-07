#!/usr/bin/env python3
"""oga-ops-check.py -- the backend ops tables are POSITIONAL initializers.

WHY THIS EXISTS
---------------
`OgaCoreOps` is initialized positionally in four places (Core/oga_core.cpp's GBA,
PSP and NES tables, Core/pokecore.cpp's DS table). Adding a field to the struct
and forgetting one table does not fail the build: every slot after the insertion
point shifts by one, so `read_audio` lands in `reader_set` and `save_state` in
`read_audio`. The result compiles, links, and calls the wrong function -- the same
shape of silent breakage as the source-list lesson in verify-core-lists.sh.

So: count the struct's fields and each table's initializers and refuse when they
disagree. Source-level on purpose -- it runs before the expensive archive build.

Counting is done on comment-stripped text, because every entry here carries a
prose annotation and the block comments contain commas of their own.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HDR = ROOT / "Core/oga_core.h"


def strip_comments(text: str) -> str:
    """Remove // and /* */ comments, string-literal aware."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            out.append(c)
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    out.append(text[i]); i += 1
                    if i < n:
                        out.append(text[i]); i += 1
                    continue
                out.append(text[i]); i += 1
            if i < n:
                out.append(text[i]); i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def struct_field_count() -> int:
    raw = HDR.read_text(encoding="utf-8")
    m = re.search(r"typedef struct OgaCoreOps\s*\{(.*?)\}\s*OgaCoreOps;", raw, re.S)
    if not m:
        sys.exit(f"!! could not find the OgaCoreOps struct in {HDR}")
    body = strip_comments(m.group(1))
    # Every field is a function pointer, so "(*" marks one. `id` is a const char*
    # and is deliberately not counted -- the tables carry it as one more entry.
    return len(re.findall(r"\(\s*\*", body))


def table_entry_count(path: Path, start_pattern: str) -> int:
    raw = path.read_text(encoding="utf-8")
    m = re.search(start_pattern + r"(.*?)\n\s*\};", raw, re.S | re.M)
    if not m:
        sys.exit(f"!! table not found in {path} (pattern {start_pattern!r})")
    body = strip_comments(m.group(1))
    body = body.replace("{", " ").replace("}", " ")
    body = body.strip()
    if not body:
        sys.exit(f"!! table body empty in {path} (pattern {start_pattern!r})")
    # A positional initializer's top-level comma count + 1 == its entry count,
    # UNLESS the list has a trailing comma (this repo's NES table does), which
    # already accounts for the final entry.
    entries = body.count(",") + (0 if body.endswith(",") else 1)
    return entries


def main() -> int:
    fields = struct_field_count()
    if fields < 10:
        sys.exit(f"!! parsed only {fields} fields out of {HDR} -- the struct shape changed")
    # +1: the tables' first entry is the id string, which is not a function pointer.
    expected = fields + 1
    print(f"== OgaCoreOps declares {fields} function-pointer slots "
          f"(+ the id string = {expected} entries per table)")
    print()

    tables = [
        (ROOT / "Core/oga_core.cpp", r"OgaCoreOps kGbaOps\s*=\s*\{", "kGbaOps"),
        (ROOT / "Core/oga_core.cpp", r"OgaCoreOps kPspOps\s*=\s*\{", "kPspOps"),
        (ROOT / "Core/oga_core.cpp", r"OgaCoreOps kNesOps\s*=\s*\{", "kNesOps"),
        (ROOT / "Core/pokecore.cpp", r"OgaCoreOps kTable\s*=\s*\{", "kDsOps"),
    ]

    print(f"== every positional table must hold exactly {expected} entries")
    failed = False
    for path, pattern, label in tables:
        got = table_entry_count(path, pattern)
        if got == expected:
            print(f"  ok  {label:8s} {got:2d} entries   ({path.name})")
        else:
            print(f"  !!  {label:8s} {got:2d} entries, expected {expected} -- THE TABLE IS ROTATED")
            failed = True

    print()
    if failed:
        print("FAIL: a backend ops table does not match OgaCoreOps. Add the missing slot(s).",
              file=sys.stderr)
        return 1
    print("PASS: every backend ops table matches the struct's slot count.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
