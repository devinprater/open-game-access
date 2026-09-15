#!/usr/bin/env python3
"""extract-nds-arm9.py — pull the ARM9/ARM7 binaries and overlay table out of a DS ROM.

⛔ WHY THIS EXISTS AND WHAT IT IS NOT. This reads the user's OWN ROM on their OWN
machine to get binaries for static analysis. It does not download anything, and the
output must never be committed: the binaries are Nintendo's copyrighted code. The
output directory is added to .gitignore and the extractor refuses to write inside the
repository.

NDS ROM header layout used here (offsets from the start of the ROM):
  0x20  u32  arm9_rom_offset      0x2C  u32  arm9_size
  0x24  u32  arm9_entry_address   0x30  u32  arm9_ram_address
  0x28  u32  arm9_ram_address     0x34  u32  arm7_rom_offset
  0x2C  u32  arm9_size            0x38  u32  arm7_entry_address
  0x30  u32  arm7_rom_offset      0x3C  u32  arm7_ram_address
  0x34  u32  arm7_entry_address   0x40  u32  arm7_size
  0x38  u32  arm7_ram_address
  0x3C  u32  arm7_size
  0x50  u32  overlay_table_offset
  0x54  u32  overlay_table_size
"""
import pathlib
import struct
import sys


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__ + "\nusage: extract-nds-arm9.py <rom.nds> <outdir>")
    rom_path = pathlib.Path(sys.argv[1])
    outdir = pathlib.Path(sys.argv[2])

    # Safety: never write Nintendo code into the repo.
    if "open-game-access" in str(outdir).lower():
        raise SystemExit("!! refusing to extract into the OGA repository — the output "
                         "is copyrighted game code. Use a directory outside the repo.")

    data = rom_path.read_bytes()
    if len(data) < 0x60:
        raise SystemExit("!! file too small to be a DS ROM")

    title = data[0:12].decode("ascii", "replace").rstrip("\0")
    gamecode = data[12:16].decode("ascii", "replace")
    print(f"ROM     : {rom_path.name}")
    print(f"title   : {title}")
    print(f"gamecode: {gamecode}")

    arm9_off, arm9_entry, arm9_ram, arm9_size = u32(data, 0x20), u32(data, 0x24), u32(data, 0x28), u32(data, 0x2C)
    arm7_off, arm7_entry, arm7_ram, arm7_size = u32(data, 0x30), u32(data, 0x34), u32(data, 0x38), u32(data, 0x3C)
    ovl_off, ovl_size = u32(data, 0x50), u32(data, 0x54)

    print(f"\nARM9  rom=0x{arm9_off:08X} entry=0x{arm9_entry:08X} ram=0x{arm9_ram:08X} size={arm9_size} "
          f"(ends 0x{arm9_ram + arm9_size:08X})")
    print(f"ARM7  rom=0x{arm7_off:08X} entry=0x{arm7_entry:08X} ram=0x{arm7_ram:08X} size={arm7_size}")
    print(f"OVL   table=0x{ovl_off:08X} size={ovl_size} ({ovl_size // 32} entries)")

    outdir.mkdir(parents=True, exist_ok=True)

    # Raw binaries (for Ghidra import with an explicit processor + base address).
    (outdir / "arm9.bin").write_bytes(data[arm9_off:arm9_off + arm9_size])
    (outdir / "arm7.bin").write_bytes(data[arm7_off:arm7_off + arm7_size])
    print(f"\nwrote {outdir / 'arm9.bin'}  ({arm9_size} bytes)")
    print(f"wrote {outdir / 'arm7.bin'}  ({arm7_size} bytes)")

    # Overlay table: 32 bytes per entry, the file id list gives each overlay's size.
    overlays = []
    if ovl_off and ovl_size:
        n = ovl_size // 32
        (outdir / "overlays").mkdir(exist_ok=True)
        for i in range(n):
            base = ovl_off + i * 32
            ov_id = u32(data, base + 0x00)
            ram_addr = u32(data, base + 0x04)
            ram_size = u32(data, base + 0x08)
            flags = u32(data, base + 0x0C)
            fstart = u32(data, base + 0x10)
            fend = u32(data, base + 0x14)
            # Size comes from the file id range in the FAT, not from ram_size: an
            # overlay's RAM size includes bss and is larger than what is in the file.
            fsize = 0
            # The FAT is at 0x48 (offset) / 0x4C (size); each entry is 8 bytes.
            fat_off, fat_size = u32(data, 0x48), u32(data, 0x4C)
            if fat_off and fat_size and fend >= fstart:
                for fid in range(fstart, fend + 1):
                    e = fat_off + fid * 8
                    if e + 8 > len(data):
                        break
                    s, en = u32(data, e), u32(data, e + 4)
                    fsize += en - s
            overlays.append((ov_id, ram_addr, ram_size, flags, fstart, fend, fsize))

        # Concatenate each overlay's file data into its own .bin.
        written = 0
        for (ov_id, ram_addr, ram_size, flags, fstart, fend, fsize) in overlays:
            blob = bytearray()
            for fid in range(fstart, fend + 1):
                e = fat_off + fid * 8
                if e + 8 > len(data):
                    break
                s, en = u32(data, e), u32(data, e + 4)
                blob += data[s:en]
            if blob:
                (outdir / "overlays" / f"ov{ov_id:03d}.bin").write_bytes(bytes(blob))
                written += 1
        print(f"wrote {written} overlay binaries to {outdir / 'overlays'}")

        # A table the Ghidra import can use to place each overlay at its RAM address.
        lines = ["id\tram_address\tram_size\tfile_id_start\tfile_id_end\tfile_bytes"]
        for (ov_id, ram_addr, ram_size, flags, fstart, fend, fsize) in overlays:
            lines.append(f"{ov_id}\t0x{ram_addr:08X}\t{ram_size}\t{fstart}\t{fend}\t{fsize}")
        (outdir / "overlay_table.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"wrote {outdir / 'overlay_table.tsv'}")


if __name__ == "__main__":
    main()
