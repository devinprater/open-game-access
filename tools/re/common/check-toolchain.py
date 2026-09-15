#!/usr/bin/env python3
"""Report the installed oga-re toolchain versions from scoop manifests.

⛔ Windows Python cannot read MSYS-style paths like /c/Users/... — the shell and the
interpreter disagree about what a path is. Resolve from %USERPROFILE% instead.
"""
import json
import os
import pathlib
import shutil
import subprocess

home = pathlib.Path(os.environ.get("USERPROFILE", os.path.expanduser("~")))
apps = home / "scoop" / "apps"

want = ["git", "7zip", "temurin21-jdk", "ghidra", "ninja", "cmake", "make",
        "rust", "rustup-msvc"]

print(f"scoop apps dir: {apps}\n")
for name in want:
    man = apps / name / "current" / "manifest.json"
    if not man.exists():
        print(f"{name:<16} NOT INSTALLED")
        continue
    try:
        vers = json.loads(man.read_text(encoding="utf-8")).get("version", "?")
    except Exception as e:                                    # noqa: BLE001
        vers = f"(manifest unreadable: {e})"
    print(f"{name:<16} {vers}")

print("\n--- on PATH ---")
for exe, args in [("python", ["--version"]), ("java", ["-version"]),
                  ("git", ["--version"]), ("ninja", ["--version"]),
                  ("cmake", ["--version"]), ("make", ["--version"]),
                  ("rustc", ["--version"]), ("cargo", ["--version"])]:
    path = shutil.which(exe)
    if not path:
        print(f"{exe:<8} not on PATH")
        continue
    try:
        r = subprocess.run([path] + args, capture_output=True, text=True, timeout=30)
        out = (r.stdout or r.stderr).strip().splitlines()
        print(f"{exe:<8} {out[0] if out else '?'}")
    except Exception as e:                                    # noqa: BLE001
        print(f"{exe:<8} (error: {e})")

print("\n--- env ---")
for var in ("JAVA_HOME", "GHIDRA_INSTALL_DIR"):
    print(f"{var} = {os.environ.get(var, 'unset')}")

print("\n--- ghidra launcher ---")
for cand in ("ghidraRun.bat", "ghidraRun"):
    print(f"{cand}: {shutil.which(cand) or 'not on PATH'}")
