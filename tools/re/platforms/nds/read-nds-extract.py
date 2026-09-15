#!/usr/bin/env python3
"""read-nds-extract.py — inventory a `dsd rom extract` output: modules, load addresses,
sizes, and the resulting Ghidra import plan.

⛔ WHY THIS EXISTS. `dsd rom extract` gives directories of .bin files but does NOT say
where each module loads at runtime — that lives in the overlay table in the ROM header
and in each module's own header. Without load addresses, a Ghidra import puts overlay
code at the wrong address and every cross-reference from ARM9 into an overlay is wrong,
which looks like "the decompiler found nothing" rather than "we mapped it wrong".

It reads the ROM header once (the authoritative source of overlay RAM addresses) and
pairs it with the extracted files. Output is a JSON import plan plus a human summary.
"""
import json
import pathlib
import struct
import sys


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__ + "\nusage: read-nds-extract.py <rom.nds> <extracted-dir>")
    rom = pathlib.Path(sys.argv[1])
    ext = pathlib.Path(sys.argv[2])

    if not ext.is_dir():
        raise SystemExit(f"!! extracted dir not found: {ext}")
    data = rom.read_bytes()

    arm9_off, arm9_entry, arm9_ram, arm9_size = (u32(data, 0x20), u32(data, 0x24),
                                                u32(data, 0x28), u32(data, 0x2C))
    arm7_off, arm7_entry, arm7_ram, arm7_size = (u32(data, 0x30), u32(data, 0x34),
                                                u32(data, 0x38), u32(data, 0x3C))
    ovl_table, ovl_size = u32(data, 0x50), u32(data, 0x54)
    fat_off, fat_size = u32(data, 0x48), u32(data, 0x4C)

    modules = []

    # --- main ARM9 ---
    p = ext / "arm9" / "arm9.bin"
    if p.is_file():
        modules.append({"module": "arm9", "file": str(p.relative_to(ext)),
                        "loadAddress": f"0x{arm9_ram:08X}", "entry": f"0x{arm9_entry:08X}",
                        "fileSize": p.stat().st_size, "headerSize": arm9_size,
                        "processor": "ARM:LE:32:v5t"})

    # --- ITCM / DTCM autoloads ---
    # These load at the TOP of the address space, not in main RAM. Wrong addresses here
    # mean functions that live in ITCM appear missing entirely.
    for name, addr in (("itcm", 0x01FF8000), ("dtcm", 0x027E0000)):
        p = ext / "arm9" / f"{name}.bin"
        if p.is_file():
            modules.append({"module": name, "file": str(p.relative_to(ext)),
                            "loadAddress": f"0x{addr:08X}", "entry": None,
                            "fileSize": p.stat().st_size, "headerSize": None,
                            "processor": "ARM:LE:32:v5t"})

    # --- ARM7 ---
    p = ext / "arm7" / "arm7.bin"
    if p.is_file():
        modules.append({"module": "arm7", "file": str(p.relative_to(ext)),
                        "loadAddress": f"0x{arm7_ram:08X}", "entry": f"0x{arm7_entry:08X}",
                        "fileSize": p.stat().st_size, "headerSize": arm7_size,
                        "processor": "ARM:LE:32:v4t"})

    # --- ARM9 overlays: RAM address from the ROM header's overlay table ---
    n_ovl = ovl_size // 32 if ovl_table and ovl_size else 0
    ovl_meta = {}
    for i in range(n_ovl):
        base = ovl_table + i * 32
        ov_id = u32(data, base + 0x00)
        ram_addr = u32(data, base + 0x04)
        ram_size = u32(data, base + 0x08)
        flags = u32(data, base + 0x0C)
        ovl_meta[ov_id] = {"ramAddress": ram_addr, "ramSize": ram_size, "flags": flags}

    ovl_dir = ext / "arm9_overlays"
    if ovl_dir.is_dir():
        for p in sorted(ovl_dir.glob("ov*.bin")):
            try:
                ov_id = int(p.stem.replace("ov", ""))
            except ValueError:
                continue
            m = ovl_meta.get(ov_id, {})
            modules.append({
                "module": f"arm9_overlay_{ov_id:03d}",
                "file": str(p.relative_to(ext)),
                "loadAddress": f"0x{m['ramAddress']:08X}" if "ramAddress" in m else None,
                "entry": None,
                "fileSize": p.stat().st_size,
                "headerSize": None,
                "ramSize": m.get("ramSize"),
                "processor": "ARM:LE:32:v5t",
                "note": "bss (ramSize > fileSize) is zero-filled at load and has no bytes in the file",
            })

    # --- summary ---
    print(f"ROM: {rom.name}")
    print(f"ARM9  load 0x{arm9_ram:08X}  entry 0x{arm9_entry:08X}  size {arm9_size}")
    print(f"ARM7  load 0x{arm7_ram:08X}  entry 0x{arm7_entry:08X}  size {arm7_size}")
    print(f"FAT   offset 0x{fat_off:08X}  size {fat_size} ({fat_size // 8} files)")
    print(f"OVL   table 0x{ovl_table:08X}  {n_ovl} overlays\n")

    print(f"{'module':<22}{'load addr':<12}{'file bytes':>11}{'ram bytes':>11}  file")
    for m in modules:
        ram = m.get("ramSize") or m.get("fileSize")
        print(f"{m['module']:<22}{str(m['loadAddress']):<12}"
              f"{m['fileSize']:>11}{ram:>11}  {m['file']}")

    bss = [m["module"] for m in modules if m.get("ramSize") and m["ramSize"] > m["fileSize"]]
    print(f"\noverlays with bss (ram > file): {len(bss)} of {len(ovl_meta)}")

    out = ext / "oga_import_plan.json"
    out.write_text(json.dumps({
        "rom": rom.name,
        "gameCode": data[12:16].decode("ascii", "replace"),
        "modules": modules,
        "notes": [
            "loadAddress comes from the ROM header's overlay table, which is authoritative.",
            "Overlay ramSize includes bss; the file is smaller and bss is zero-filled at load.",
            "Import each module as a separate Ghidra program at its loadAddress so "
            "cross-references between modules resolve.",
        ],
    }, indent=2), encoding="utf-8")
    print(f"\nwrote {out}")


if __name__ == "__main__":
    main()
