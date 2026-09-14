#!/usr/bin/env python3
"""portable-toolchain.py — replace GNU/Linux-only assumptions in the build scripts.

The CI failure was not one bug but a class of them: scripts written on Linux/WSL
that silently assumed GNU userland. On a macOS runner each assumption fails
differently and misleadingly:

  * `nproc`              -> "command not found", then xargs runs with no -P
  * `xargs -a file`      -> "invalid option -- a" (GNU-only), so NOTHING compiles,
                            yet the script exits 0 and later reports "poke
                            symbols: 0" — the real error is two steps away
  * /usr/local/swift/... -> "No such file or directory" for llvm-ar, so the
                            archive is never created

This rewrites those to portable equivalents, keeping the Linux behaviour as the
preferred path when it is available.
"""
import glob
import os
import re

here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)

JOBS_BLOCK = '''# ---- parallelism, GNU or BSD ----
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  else JOBS=4; fi
fi
'''

TOOL_BLOCK = '''# ---- LLVM binutils, located rather than assumed ----
# GNU ar's index is not readable by ld64.lld, and GNU nm cannot read Mach-O, so
# these must come from the same family as the linker.
LLVM_AR=""; LLVM_RANLIB=""; LLVM_NM=""
for d in /usr/local/swift/bin /usr/bin /opt/homebrew/opt/llvm/bin; do
  [ -x "$d/llvm-ar" ]     && [ -z "$LLVM_AR" ]     && LLVM_AR="$d/llvm-ar"
  [ -x "$d/llvm-ranlib" ] && [ -z "$LLVM_RANLIB" ] && LLVM_RANLIB="$d/llvm-ranlib"
  [ -x "$d/llvm-nm" ]     && [ -z "$LLVM_NM" ]     && LLVM_NM="$d/llvm-nm"
done
[ -z "$LLVM_AR" ]     && LLVM_AR="$(command -v llvm-ar || command -v ar)"
[ -z "$LLVM_RANLIB" ] && LLVM_RANLIB="$(command -v llvm-ranlib || command -v ranlib)"
[ -z "$LLVM_NM" ]     && LLVM_NM="$(command -v llvm-nm || command -v nm)"
'''

report = []

for path in sorted(glob.glob("*.sh")):
    s = open(path, encoding="utf-8", errors="replace").read()
    orig = s

    # 1. xargs -a <file> -> stdin
    s = s.replace('xargs -a "$OBJ/list.txt"', 'xargs')

    # 2. $(nproc) inline -> $JOBS, and define JOBS once
    if "$(nproc)" in s or "nproc" in s:
        s = s.replace('-P "$(nproc)"', '-P "$JOBS"')
        if "JOBS=" not in s:
            s = s.replace("set -uo pipefail\n", "set -uo pipefail\n" + JOBS_BLOCK, 1)
            if JOBS_BLOCK not in s:
                s = s.replace("set -euo pipefail\n", "set -euo pipefail\n" + JOBS_BLOCK, 1)
        s = re.sub(r'^JOBS="\$\{JOBS:-\(nproc\)\}"\n', '', s, flags=re.M)
        s = re.sub(r'^JOBS="\$\{JOBS:-\$\(nproc\)\}"\n', '', s, flags=re.M)

    # 3. /usr/local/swift/bin/<tool> -> located variables
    if "/usr/local/swift/bin/llvm-ar" in s:
        s = s.replace('AR=/usr/local/swift/bin/llvm-ar', 'AR="$LLVM_AR"')
        s = s.replace('RANLIB=/usr/local/swift/bin/llvm-ranlib', 'RANLIB="$LLVM_RANLIB"')
        if "LLVM_AR=" not in s:
            s = s.replace("set -uo pipefail\n", "set -uo pipefail\n" + TOOL_BLOCK, 1)
    s = s.replace('/usr/local/swift/bin/llvm-nm', '"$LLVM_NM"')
    s = s.replace('/usr/local/swift/bin/llvm-objdump',
                  '"$(command -v llvm-objdump || echo /usr/bin/otool)"')
    # clang paths: keep the Linux ones as a preference, fall back if absent
    if 'CXX="/usr/local/swift/bin/clang++"' in s:
        s = s.replace('CXX="/usr/local/swift/bin/clang++"',
                      'CXX="${CXX:-/usr/local/swift/bin/clang++}"\n'
                      '[ -x "$CXX" ] || CXX="$(command -v clang++ || echo clang++)"')
        s = s.replace('CC="/usr/local/swift/bin/clang"',
                      'CC="${CC:-/usr/local/swift/bin/clang}"\n'
                      '[ -x "$CC" ] || CC="$(command -v clang || echo clang)"')

    if s != orig:
        open(path, "w", encoding="utf-8").write(s)
        report.append(path)

print(f"== made portable: {len(report)} script(s)")
for p in report:
    print("   ", p)
print()
print("== remaining GNU-only assumptions ==")
found = False
for path in glob.glob("*.sh"):
    for i, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        if re.search(r'xargs -a|nproc|/usr/local/swift/bin/llvm', line):
            print(f"  {path}:{i}: {line.rstrip()}")
            found = True
if not found:
    print("  none")
