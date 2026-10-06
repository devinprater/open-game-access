#!/usr/bin/env python3
"""Resolve ALL message text from data_sys_us.afs using the #MSG format proved in the ELF.

The format (from FUN_000da040, the game's own loader):
    byte 0     '#' = not loaded, '!' = loaded
    1..3       "MSG"
    +0x12 u16  COUNT
    +0x14 u32  relative offset -> array[COUNT] u32, each relative to the container base  (NAMES)
    +0x18 u32  relative offset -> array[COUNT] u32, relative likewise                      (TEXT)

Text is Unicode (mostly). This scans the whole 81 MB archive, parses every container it can,
and writes id -> name -> text.
"""
import struct

AFS = r"C:\Users\Devin Prater\AppData\Local\Temp\dbz-ar-extract\data_sys_us.afs"
OUT = r"C:\Users\Devin Prater\AppData\Local\Temp\dbz-afs-msg-resolved.txt"

data = open(AFS, "rb").read()
print(f"afs {len(data):,} bytes")

bases = []
i = data.find(b"#MSG")
while i != -1:
    bases.append(i)
    i = data.find(b"#MSG", i + 1)
print(f"found {len(bases)} '#MSG' containers")


def wide_string(base, rel, limit=0x4000):
    a = base + rel
    if rel == 0 or a <= 0 or a + 2 > len(data):
        return None
    end = a
    while end + 1 < len(data) and end - a < limit and not (data[end] == 0 and data[end + 1] == 0):
        end += 2
    raw = data[a:end]
    if len(raw) < 2:
        return None
    try:
        return raw.decode("utf-16le")
    except Exception:
        return raw.decode("latin1", "replace")


def parse(base):
    try:
        count = struct.unpack_from("<H", data, base + 0x12)[0]
        off14 = struct.unpack_from("<I", data, base + 0x14)[0]
        off18 = struct.unpack_from("<I", data, base + 0x18)[0]
    except struct.error:
        return []
    if count == 0 or count > 20000 or off14 == 0 or off18 == 0:
        return []
    if base + off14 + count * 4 > len(data) or base + off18 + count * 4 > len(data):
        return []
    rows = []
    for n in range(count):
        try:
            rel14 = struct.unpack_from("<I", data, base + off14 + n * 4)[0]
            rel18 = struct.unpack_from("<I", data, base + off18 + n * 4)[0]
        except struct.error:
            break
        name = None
        if rel14 and 0 < base + rel14 < len(data):
            e = data.find(b"\x00", base + rel14)
            if e > base + rel14:
                name = data[base + rel14:e].decode("latin1", "replace")
        text = wide_string(base, rel18) if rel18 else None
        if name or text:
            rows.append((n, name, text))
    return rows


allrows = []
ok = 0
for n, base in enumerate(bases):
    rows = parse(base)
    if not rows:
        continue
    ok += 1
    for idx, name, text in rows:
        allrows.append((base, idx, name, text))

print(f"parsed {ok}/{len(bases)} containers; {len(allrows)} message rows")

# keep only rows whose text looks like real prose (not binary)
good = []
for base, idx, name, text in allrows:
    if not text:
        continue
    printable = sum(ch.isprintable() or ch in "\n\r\t" for ch in text)
    if len(text) and printable / len(text) > 0.8 and any(c.isalpha() for c in text):
        good.append((base, idx, name, text))
print(f"{len(good)} rows with real text")

with open(OUT, "w", encoding="utf-8") as f:
    for base, idx, name, text in good:
        flat = text.replace("\n", "\\n").replace("\r", "")
        f.write(f"[0x{base:07X}:{idx}] {name}\t{flat}\n")
print(f"wrote {OUT}")

# show the story families
import re
print("\n=== counts by family ===")
fam = {}
for base, idx, name, text in good:
    if not name:
        continue
    key = re.sub(r"_\d+$", "_N", name)
    fam[key] = fam.get(key, 0) + 1
for k, v in sorted(fam.items(), key=lambda x: -x[1])[:25]:
    print(f"  {v:>5}  {k}")

print("\n=== sample story lines (MSG_AR_*) ===")
n = 0
for base, idx, name, text in good:
    if name and name.startswith("MSG_AR"):
        print(f"  {name:<34} {text[:80]!r}")
        n += 1
        if n >= 25:
            break
