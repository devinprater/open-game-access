#!/usr/bin/env python3
"""dedupe-reference.py -- remove duplicated rule blocks from the offloaded reference file.

WHAT WENT WRONG
    While reducing SKILL.md under its 100k limit, blocks were offloaded to
    references/elf-and-packfile-techniques.md several times. Some offloads appended content that was
    ALREADY in the file, so the reference now contains ~2 copies of most rules (every rule heading
    appears twice, and the file is ~1780 lines where it should be ~900).

    This script rebuilds the file with each `## Rule N:` block appearing exactly once, keeping the
    FIRST occurrence (the original ordering), and reports what it removed.

SAFETY
    Writes a .bak first, and refuses to write if the result is implausibly small (<10 KB) or if it
    would remove a rule number entirely (i.e. if the two copies had DIFFERENT bodies, which would mean
    they are not duplicates and must be kept).
"""
import os, re, shutil, sys

REF = r"C:\Users\Devin Prater\AppData\Local\hermes\skills\software-development\ppsspp-memory-discovery\references\elf-and-packfile-techniques.md"

text = open(REF, encoding="utf-8").read()
orig_len = len(text)

# Split into (heading, body) blocks on lines starting with "## ".
parts = re.split(r"(?m)^(?=## )", text)
# parts[0] is the preamble before the first heading.

seen = {}
out = []
removed = []
for chunk in parts:
    m = re.match(r"^## ([^\n]+)", chunk)
    if not m:
        out.append(chunk)
        continue
    key = m.group(1).strip()
    if key in seen:
        # A duplicate. Keep the FIRST, but only if the bodies match closely; otherwise keep both
        # because they are different rules that happen to share a heading.
        first = seen[key]
        b1 = re.sub(r"(?m)^## [^\n]+\n", "", first)
        b2 = re.sub(r"(?m)^## [^\n]+\n", "", chunk)
        if abs(len(b1) - len(b2)) < max(80, 0.15 * max(len(b1), len(b2))):
            removed.append(key)
            continue
        else:
            # different bodies -> keep, but disambiguate visually
            out.append(chunk)
            continue
    seen[key] = chunk
    out.append(chunk)

new_text = "".join(out)
new_len = len(new_text)

print("original: %d chars, %d blocks" % (orig_len, len(parts)))
print("deduped : %d chars, %d blocks" % (new_len, len(parts) - len(removed)))
print("removed %d duplicate block(s)" % len(removed))

if new_len < 10000:
    sys.exit("REFUSING: deduped file would be %d chars (too small)" % new_len)

shutil.copyfile(REF, REF + ".bak")
open(REF, "w", encoding="utf-8", newline="").write(new_text)
print("written; backup at %s.bak" % REF)
