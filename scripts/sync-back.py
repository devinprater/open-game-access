#!/usr/bin/env python3
"""sync-back.py -- copy the WSL git tree's Dissidia doc back to the Windows working tree.

WHY THIS EXISTS
    `scripts/sync-dissidia.sh` copies Windows -> WSL. When a doc edit is made in the WSL tree
    (e.g. recovering a clobbered file from git), the Windows copy must be updated too, or the next
    sync would push the stale Windows version back over it.

    A clobber incident motivates this: a shell command of the form `cp something "$D"` where `$D` had
    been reused as the doc path overwrote the doc with a Java file, and it was then committed. This
    script's checks (size + section count) are the guard that would have caught it.
"""
import os, shutil, sys

SRC = r"\\wsl$\Ubuntu-24.04\home\devin\oga-work\docs\reverse-engineering\dissidia-final-fantasy.md"
DST = r"C:\Users\Devin Prater\open-game-access\docs\reverse-engineering\dissidia-final-fantasy.md"

# Fall back to a copy through the WSL command if the UNC path is unavailable.
if not os.path.exists(SRC):
    import subprocess
    tmp = os.path.join(os.path.expandvars(r"%LOCALAPPDATA%"), "Temp", "doc-from-wsl.md")
    subprocess.run(["wsl.exe", "-d", "Ubuntu-24.04", "--", "bash", "-lc",
                    "cat /home/devin/oga-work/docs/reverse-engineering/dissidia-final-fantasy.md"],
                   stdout=open(tmp, "wb"), check=True)
    SRC = tmp

data = open(SRC, encoding="utf-8").read()
n = data.count("\n## ")
size = len(data.encode())

# GUARD: the real doc is tens of KB with 15+ sections. Refuse to write something that is not.
if size < 20000 or n < 10:
    sys.exit("REFUSING: source looks wrong (%d bytes, %d sections) -- not overwriting the good doc"
             % (size, n))

shutil.copyfile(SRC, DST)
print("synced %d bytes, %d sections -> %s" % (size, n, DST))
