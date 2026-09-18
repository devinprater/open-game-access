#!/usr/bin/env python3
"""mark-stale-reports.py -- annotate Ghidra report files produced by the SUPERSEDED project.

WHY
    Two Ghidra projects exist for Dissidia:
        DISSIDIA      <- imported with BinaryLoader at base 0x08804000 (WRONG: addresses skewed 0x74)
        DISSIDIA_ELF  <- imported with ElfLoader (CORRECT: matches the vaddrs the code references)

    Reports written by the DISSIDIA project are superseded, but they sit on disk looking authoritative.
    `stringrefs-report.txt` literally says "scanned 783694 instructions, 0 candidate match(es)" -- a
    future session (or the user) could read that as "the game never references these strings", which is
    FALSE: the corrected run found 23 hits.

    So annotate them in place. Do not delete: the files are evidence of the mistake and how it looked.
"""
import os, sys

DIR = r"C:\Users\Devin Prater\oga-ghidra-dissidia"

BANNERS = {
    "stringrefs-report.txt": (
        "!!! SUPERSEDED -- DO NOT READ AS EVIDENCE !!!\n"
        "Produced by the DISSIDIA project, which was imported with BinaryLoader at base 0x08804000.\n"
        "That maps file offset N to base+N, but MIPS code references the ELF vaddr, so every address\n"
        "in this scan was skewed by 0x74 (the first LOAD segment is file_off 0x74 / vaddr 0).\n"
        "Hence '0 candidate matches' is an ARTIFACT OF THE WRONG ADDRESSES, not an absence.\n"
        "The corrected run against DISSIDIA_ELF (searching Ghidra memory for the string bytes)\n"
        "found 23 string occurrences and decompiled 10 referring functions -- see\n"
        "strings-report.txt and doc section 15.\n"
    ),
    "loaders-report.txt": (
        "!!! SUPERSEDED -- same reason as stringrefs-report.txt !!!\n"
        "Written by the DISSIDIA (BinaryLoader) project. Its '0 references' result is an artifact of\n"
        "the 0x74 address skew, not evidence of absence. Use strings-report.txt instead.\n"
    ),
}

for name, banner in BANNERS.items():
    p = os.path.join(DIR, name)
    if not os.path.exists(p):
        print("missing (skipped): %s" % name)
        continue
    with open(p, encoding="utf-8", errors="replace") as f:
        body = f.read()
    if body.startswith("!!!"):
        print("already marked: %s" % name)
        continue
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(banner + "\n" + "-" * 70 + "\n\n" + body)
    print("marked superseded: %-24s -> %d bytes" % (name, os.path.getsize(p)))
