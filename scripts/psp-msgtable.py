#!/usr/bin/env python
"""psp-msgtable.py — locate the message-id -> text map in the decrypted ELF.

WHY: this closes character identity. Established so far (see the doc):
  - the game stores MESSAGE IDS, not string pointers; FUN_0883ee74(id, -1) turns an id into text
  - the id table at 0x08A75538 holds ids: 505 506 507 508 509 510 171 172 169 170 173 174 162 163
  - another id table at 0x08A72100 holds 1109 1109 1109 1110 1111 ... 1113
  - the roster NAME strings are resident at 0x08C85C82 (UTF-16LE, 48 entries in select order)

If a message table maps id -> text, searching the ELF for those id values will reveal it. This
script scans the whole PT_LOAD segment for occurrences of selected ids as u32/u16 and reports the
surrounding structure, so the table's layout becomes visible.

Usage:
    python scripts/psp-msgtable.py <EBOOT.dec> 505 506 1109
    python scripts/psp-msgtable.py <EBOOT.dec> --scan-strings
"""
import argparse
import struct
import sys
import importlib.util as u

HERE = __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0]
spec = u.spec_from_file_location("pew", HERE + "/psp-elf-window.py")
pew = u.module_from_spec(spec)
spec.loader.exec_module(pew)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("ids", nargs="*", type=int, help="message ids to locate")
    ap.add_argument("--ctx", type=int, default=8, help="words of context to show")
    ap.add_argument("--min-hits", type=int, default=0)
    ap.add_argument("--scan-strings", action="store_true",
                    help="scan the load segment for UTF-16 string runs")
    args = ap.parse_args()

    data = open(args.elf, "rb").read()
    phdrs = pew.parse_phdrs(data)
    loads = [p for p in phdrs if p[0] == 1]

    for mid in args.ids:
        print(f"=== searching for message id {mid} (0x{mid:04X}) as u32 ===")
        pat = struct.pack("<I", mid)
        hits = []
        i = data.find(pat)
        while i >= 0:
            hits.append(i)
            i = data.find(pat, i + 1)
        # map file offset back to a vaddr
        def off_to_addr(o):
            for (_t, p_off, p_va, p_fsz, _msz, _fl) in loads:
                if p_off <= o < p_off + p_fsz:
                    return p_va + (o - p_off)
            return None
        print(f"  {len(hits)} raw occurrence(s) in the file")
        shown = 0
        for o in hits:
            a = off_to_addr(o)
            if a is None:
                continue
            # show surrounding words
            ctx = []
            for k in range(-args.ctx, args.ctx + 1):
                oo = o + k * 4
                if 0 <= oo <= len(data) - 4:
                    ctx.append(struct.unpack_from("<I", data, oo)[0])
                else:
                    ctx.append(-1)
            print(f"   @0x{a:08X}  ctx: " + " ".join(
                ("[%d]" % v) if v == mid else str(v) for v in ctx))
            shown += 1
            if shown >= 12:
                print("   ... (more omitted)")
                break
        print()

    if args.scan_strings:
        print("=== UTF-16 string runs inside the load segment (first 40 of length>=8) ===")
        # scan for plausible UTF-16LE text
        found = 0
        for (_t, p_off, p_va, p_fsz, _msz, _fl) in loads:
            seg = data[p_off:p_off + p_fsz]
            i = 0
            while i < len(seg) - 4 and found < 40:
                # try a run of printable utf16
                j = i
                chars = 0
                while j < len(seg) - 1:
                    c = struct.unpack_from("<H", seg, j)[0]
                    if 32 <= c < 0x2500 or c in (0x0A,):
                        chars += 1
                        j += 2
                    else:
                        break
                if chars >= 8:
                    txt = seg[i:j].decode("utf-16-le", "replace").replace("\n", "\\n")
                    print(f"   0x{p_va + i:08X}  {txt[:70]!r}")
                    found += 1
                    i = j + 2
                else:
                    i += 2
        print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
