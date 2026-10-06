#!/usr/bin/env python3
"""MSG-ID -> TEXT resolver for DBZ: Shin Budokai - Another Road.

The container format is documented by the game's OWN loader, FUN_000da040:
    byte 0        '#' = not yet loaded, '!' = loaded
    bytes 1..3    "MSG"
    +0x12  u16    COUNT
    +0x14  u32    RELATIVE offset -> array[COUNT] of u32, each relative to the container base
    +0x18  u32    RELATIVE offset -> second array[COUNT] of u32, relative likewise

Measured on the first container: the +0x14 array holds the ASCII message NAMES
("MSG_SYS_MS_0", ...) and the +0x18 array holds the message TEXT.

Text encoding: the +0x18 strings are NOT single characters -- scanning for a plain NUL byte
truncates them to one character ('M'), which is the giveaway for UTF-16LE. So the text is
decoded as UTF-16LE with an SJIS fallback.

Output: id -> name -> text, for every #MSG container in the ELF.
"""
import struct
import sys

ELF = r"C:\Users\Devin Prater\AppData\Local\Temp\dbz-ar-extract\EBOOT.dec"
data = open(ELF, "rb").read()
SKEW = 0x74

def find_containers():
    """Every '#MSG' in the file."""
    out = []
    i = data.find(b"#MSG")
    while i != -1:
        out.append(i)
        i = data.find(b"#MSG", i + 1)
    return out

def rel_string(base, rel, wide_hint=False):
    a = base + rel
    if a <= 0 or a >= len(data):
        return None
    # UTF-16LE: read until an aligned 0000
    end = a
    while end + 1 < len(data) and not (data[end] == 0 and data[end + 1] == 0):
        end += 2
    raw = data[a:end]
    try:
        s = raw.decode("utf-16le")
    except Exception:
        s = raw.decode("latin1", "replace")
    if wide_hint:
        return s
    # if the wide decode looks like garbage, try single-byte
    if s and sum(ch.isprintable() or ch in "\n\r\t" for ch in s) < len(s) * 0.6:
        e2 = data.find(b"\x00", a)
        s = data[a:e2].decode("sjis", "replace") if e2 > a else s
    return s

def parse(base, label):
    magic = data[base:base + 4]
    count = struct.unpack_from("<H", data, base + 0x12)[0]
    off14 = struct.unpack_from("<I", data, base + 0x14)[0]
    off18 = struct.unpack_from("<I", data, base + 0x18)[0]
    print(f"\n===== {label} @ file 0x{base:06X} (vaddr 0x{base - SKEW:06X}) =====")
    print(f"  magic={magic!r} count={count} +0x14=0x{off14:X} +0x18=0x{off18:X}")
    if count == 0 or count > 20000 or off14 == 0 or off18 == 0:
        print("  (implausible header -- skipped)")
        return []
    rows = []
    for i in range(count):
        try:
            rel14 = struct.unpack_from("<I", data, base + off14 + i * 4)[0]
            rel18 = struct.unpack_from("<I", data, base + off18 + i * 4)[0]
        except struct.error:
            break
        name = None
        if rel14:
            e = data.find(b"\x00", base + rel14)
            name = data[base + rel14:e].decode("latin1", "replace") if e > base + rel14 else None
        text = rel_string(base, rel18) if rel18 else None
        rows.append((i, name, text))
    for i, name, text in rows[:12]:
        t = (text or "").replace("\n", "\\n")
        print(f"  [{i:3d}] {name!r:<32} -> {t[:70]!r}")
    if count > 12:
        print(f"  ... ({count} entries)")
    return rows

containers = find_containers()
print(f"found {len(containers)} '#MSG' container(s) in the ELF")

allrows = []
for n, base in enumerate(containers):
    allrows.append((base, parse(base, f"container {n}")))

# save a flat dump
out = []
for base, rows in allrows:
    for i, name, text in rows:
        if text:
            out.append(f"[0x{base - SKEW:06X}:{i}] {name}\t{text}")
path = r"C:\Users\Devin Prater\AppData\Local\Temp\dbz-msg-resolved.txt"
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
print(f"\nwrote {len(out)} resolved message rows to {path}")
