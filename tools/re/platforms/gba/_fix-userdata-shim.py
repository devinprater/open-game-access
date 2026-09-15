#!/usr/bin/env python3
"""Rewrite the shim's HOST_* call sites to use callReal(), and drop the duplicate defs.

The shim was written against a stub host that provided `emu` as a plain table, so it
monkey-patched emu directly and cached mGBA's methods into HOST_* locals. On real mGBA
`emu` is userdata: field assignment raises "Invalid key", and the cached HOST_* locals no
longer exist. The new design shadows `emu` with a plain table delegating through callReal().

This does the mechanical rewrite so the two stale duplicate definitions (platform/framecount)
do not silently win over the new ones.
"""
import pathlib
import re

p = pathlib.Path(r"C:\Users\Devin Prater\Dropbox\programs\pokemon-access\lua\mgba_compat.lua")
lines = p.read_text(encoding="utf-8").splitlines(keepends=True)

# 1. Remove the stale duplicate emu.platform / emu.framecount blocks (they call the
#    long-gone HOST_* locals and would shadow the new definitions).
out = []
i = 0
removed = 0
while i < len(lines):
    ln = lines[i]
    if "return HOST_platform(HOST_for_self)" in ln or "return HOST_frame(HOST_for_self)" in ln:
        # drop this line and walk backwards over its now-orphaned `emu.foo = function()`
        # header plus any comment block immediately above it
        j = len(out) - 1
        while j >= 0 and out[j].strip() == "":
            j -= 1
        if j >= 0 and re.match(r"\s*emu\.(platform|framecount)\s*=\s*function", out[j]):
            del out[j]
            removed += 1
        elif j >= 1 and out[j].strip() == "end" and re.match(r"\s*emu\.(platform|framecount)\s*=\s*function", out[j-1]):
            del out[j-1:j+1]
            removed += 1
        i += 1
        continue
    out.append(ln)
    i += 1
lines = out
print(f"removed {removed} stale duplicate definition(s)")

text = "".join(lines)

# 2. Mechanical call-site rewrite: HOST_x(HOST_for_self[, args]) -> callReal("x"[, args])
subs = [
    ("HOST_read8(HOST_for_self, a)",        'callReal("read8", a)'),
    ("HOST_read16(HOST_for_self, a)",       'callReal("read16", a)'),
    ("HOST_read32(HOST_for_self, a)",       'callReal("read32", a)'),
    ("HOST_readRange(HOST_for_self, addr, length)", 'callReal("readRange", addr, length)'),
    ('HOST_readReg(HOST_for_self, mapped)', 'callReal("readRegister", mapped)'),
    ('HOST_readReg(HOST_for_self, "pc")',   'callReal("readRegister", "pc")'),
    ("HOST_read32(HOST_for_self, address)", 'callReal("read32", address)'),
    ("HOST_read32(HOST_for_self, addr)",    'callReal("read32", addr)'),
    ("HOST_getKeys(HOST_for_self)",         'callReal("getKeys")'),
]
for old, new in subs:
    n = text.count(old)
    if n:
        text = text.replace(old, new)
        print(f"  {n}x  {old}  ->  {new}")

p.write_text(text, encoding="utf-8")

leftover = [f"{k}: {l.strip()}" for k, l in enumerate(text.splitlines(), 1)
            if "HOST_" in l and "HOST_" not in ("HOST = emu",) and "host" not in l.lower().replace("host_", "")]
leftover = [x for x in leftover if "HOST_platform" in x or "HOST_for_self" in x or "HOST_read" in x or "HOST_getKeys" in x or "HOST_frame" in x]
print("leftover HOST_* references:", leftover if leftover else "none")
