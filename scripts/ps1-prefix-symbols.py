#!/usr/bin/env python3
"""ps1-prefix-symbols.py -- give every global PCSX ReARMed defines an oga_ps1_ prefix.

WHY. PCSX ReARMed is a C codebase with generic global names (Config, gpu, spu,
CdromId, filestream_open, the libretro retro_* entry points, plus libchdr, zstd,
miniz and LZMA). Several of those already exist in this app's single link: mGBA,
melonDS, Mesen, GPGX and PPSSPP vendor their own libretro-common, zlib-alikes and
compression code. Localizing a computed overlap (the GPGX approach) would leave
the next core added to the link free to collide again, and PCSX's own objects
cross-reference these names, so they cannot simply be hidden. Renaming the WHOLE
defined set is the stable fix: every definition and every reference inside the
PCSX objects becomes oga_ps1_<name>, so nothing PCSX exports can meet anything
else. Core/ps1_core.cpp calls the renamed names directly.

WHAT. Takes the PCSX objects (or one static archive of them):
  1. lists every external symbol they DEFINE (nm --defined-only --extern-only);
  2. objcopy --redefine-syms renames each one, definitions and references, in
     every given object (so cross-references between PCSX objects stay bound);
  3. re-proves the result: no given object still defines an unprefixed global.
Undefined references to libc and the OS (memcpy, fopen, pthread_*) are left
alone: they are not in the defined set.

SPELLING. Mach-O stores a C symbol `foo` as `_foo`; ELF stores it as `foo`. The
prefix goes after the C-level name's start, so --format macho maps `_foo` to
`_oga_ps1_foo` and --format elf maps `foo` to `oga_ps1_foo`. Pass the target's
format (the iOS builds are macho; host Linux and Android are elf).

Usage: ps1-prefix-symbols.py --format elf|macho [--nm NM] [--objcopy OBJCOPY] FILE...
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

PREFIX = "oga_ps1_"


def defined_globals(nm, files):
    out = subprocess.run([nm, "--defined-only", "--extern-only", "-P"] + files,
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("!! %s failed: %s" % (nm, out.stderr.strip()))
    names = set()
    for line in out.stdout.splitlines():
        parts = line.split()
        # -P (POSIX) lines are "name type [value size]"; archive headers are
        # "lib.a[member.o]:" lines with a trailing colon and no type.
        if len(parts) < 2 or parts[0].endswith(":"):
            continue
        name, typ = parts[0], parts[1]
        if typ in ("U", "w", "v"):
            continue
        names.add(name)
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--format", choices=("elf", "macho"), required=True)
    ap.add_argument("--nm", default=shutil.which("llvm-nm") or shutil.which("nm"))
    ap.add_argument("--objcopy", default=shutil.which("llvm-objcopy") or shutil.which("objcopy"))
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    if not a.nm or not a.objcopy:
        sys.exit("!! need nm and objcopy")

    files = [f for f in a.files if os.path.isfile(f)]
    if not files:
        sys.exit("!! no PCSX objects given")

    # INCREMENTAL BUILDS. A cached object was prefixed on an earlier run and
    # defines oga_ps1_bar; a freshly recompiled one still REFERENCES plain bar.
    # So the map is built from the C-level base name of every definition,
    # prefixed or not: bar -> oga_ps1_bar is applied everywhere, and an object
    # that no longer mentions bar is simply unchanged by it.
    lead = "_" if a.format == "macho" else ""
    bases = set()
    for n in defined_globals(a.nm, files):
        if lead and not n.startswith(lead):
            continue                # an assembler-local spelling; leave it
        c = n[len(lead):]
        if c.startswith(PREFIX):
            c = c[len(PREFIX):]
        bases.add(c)
    pairs = [(lead + c, lead + PREFIX + c) for c in sorted(bases)]

    if pairs:
        with tempfile.NamedTemporaryFile("w", suffix=".syms", delete=False) as t:
            for old, new in pairs:
                t.write("%s %s\n" % (old, new))
            mapfile = t.name
        try:
            for f in files:
                r = subprocess.run([a.objcopy, "--redefine-syms=" + mapfile, f],
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    sys.exit("!! objcopy failed on %s: %s" % (f, r.stderr.strip()))
        finally:
            os.unlink(mapfile)

    # Re-prove: objcopy can fail quietly, and an unprefixed PCSX global is a
    # duplicate-symbol link error waiting for the next core.
    left = []
    for n in defined_globals(a.nm, files):
        bare = n[1:] if a.format == "macho" and n.startswith("_") else n
        if not bare.startswith(PREFIX):
            left.append(n)
    if left:
        for n in sorted(left)[:20]:
            print("!! still unprefixed: %s" % n, file=sys.stderr)
        sys.exit("!! %d PCSX global(s) kept their generic names" % len(left))
    print("ps1-prefix-symbols: %d PCSX global(s) carry the prefix in %d file(s) (%s)"
          % (len(pairs), len(files), a.format))


if __name__ == "__main__":
    main()
