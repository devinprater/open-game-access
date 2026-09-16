#!/usr/bin/env python3
"""
oga-sg-reader.py — READ a Steins;Gate: My Darling's Embrace dump as a visual novel.

⭐ THIS IS THE READER for this game. Every address and formula in it was derived from the
game's own decompiled code and then verified against a live dump — not guessed.

## How the message log works (from EBOOT.dec)

  FUN_00051574():  log_base = alloc(config[0x18] * 0xAC, "MessageLog Buf")
  FUN_00050fc8(i): return log_base + ((write_head - (i - 1)) - 1) * 0xAC
  FUN_00051028(i, e): render entry i, reading the TEXT at entry + 0x1c

so the entries run NEWEST FIRST by index: index 1 is the most recent line, and the
oldest retained line is index `write_head`. That is exactly how a backlog is drawn.

## Entry layout (0xAC bytes)

  +0x00   type/flag byte (copied to the display list as the entry's kind)
  +0x1c   SPEAKER NAME  (or the dialogue text when there is no name plate)
  +0x48   DIALOGUE TEXT
  text is Shift-JIS with the game's control pairs; `%K%P` is a PAGE BREAK

⛔ THE `g` / `C` PREFIXES ARE ENCODING ARTEFACTS, NOT SPEAKER CODES.
`0x81 0x67` and `0x81 0x43` are multi-byte deliminters that render as one `latin1` byte.
Stripping them with a decode table is correct; building a speaker table from them is not.

## Address translation

Ghidra's names for this binary are offset from the real vaddr by a constant, solved from
an independently verified value (`iRam00055e34` -> the config pointer `0x0933C580`):

    real_RAM = LOAD_BASE + ghidra_name + 0x15C000       LOAD_BASE = 0x08804000

Usage:
  oga-sg-reader.py DUMP.bin [--lines 12] [--oldest]
  oga-sg-reader.py DUMP.bin --speak-last
"""
import argparse
import struct
import sys

LOAD_BASE = 0x08804000
G2R = 0x15C000                      # Ghidra-name -> real-vaddr correction
RAM_BASE = 0x08800000
RAM_SIZE = 0x01800000

# the three globals the decompiled code uses
G_LOG_BASE = 0x18F14                # iRam00018f14 — pointer to the log array
G_WRITE_HEAD = 0x197E8              # iRam000197e8 — how many entries are logged
G_CONFIG = 0x55E34                  # iRam00055e34 — pointer to the loaded SYSTEM.CFG

ST = 0xAC                           # entry stride
OFF_NAME = 0x1C                     # speaker name, or the text when there is no name
OFF_TEXT = 0x48                     # the spoken line

# The engine's control pairs. ⛔ These are DELIMITERS, not text: leaving them in makes a
# reader speak raw bytes, and mis-reading them as speaker codes is a documented trap.
CTRL = {
    b"\x81\x43": ", ",      # in-line break (comma/space)
    b"\x81\x67": "",        # start of the spoken text field
    b"\x81\x6b": "",        # start of the speaker-name field
    b"\x81\x6c": "",        # end of the speaker-name field
    b"\x81\x68": "",        # end of box
    b"%K%P": " ",           # page break — a space, never spoken
    b"%P": " ",
    b"%K": " ",
}


def addr(name_off: int) -> int:
    """Ghidra global name -> real RAM address (validated translation)."""
    return LOAD_BASE + name_off + G2R


def clean(raw: bytes) -> str:
    for k, v in CTRL.items():
        raw = raw.replace(k, v.encode())
    try:
        s = raw.decode("utf-8")
    except UnicodeDecodeError:
        s = raw.decode("shift_jis", "replace")
    return " ".join(s.split()).strip(" ,")


class Log:
    def __init__(self, dump_path):
        self.path = dump_path
        self.d = open(dump_path, "rb").read()
        if len(self.d) != RAM_SIZE:
            raise ValueError(f"expected {RAM_SIZE} bytes of user RAM, got {len(self.d)}")
        self.log_base = self.u32(addr(G_LOG_BASE))
        self.write_head = self.u32(addr(G_WRITE_HEAD))
        self.config = self.u32(addr(G_CONFIG))
        if not (RAM_BASE <= self.log_base < RAM_BASE + RAM_SIZE):
            raise ValueError(f"log base 0x{self.log_base:08X} is not in RAM — wrong dump?")
        self.capacity = self.u16(self.config + 0x18) if self.config else 0

    # ---- raw access -------------------------------------------------------
    def u32(self, a):
        return struct.unpack_from("<I", self.d, a - RAM_BASE)[0]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a - RAM_BASE)[0]

    def cstr(self, a, n=0x90):
        raw = self.d[a - RAM_BASE: a - RAM_BASE + n].split(b"\x00")[0]
        return clean(raw)

    # ---- the formula, straight from FUN_00050fc8 --------------------------
    def entry_addr(self, index: int):
        """
        index 1 = most recent. Returns None for the 'no entry' fallback slot.

        ⛔ THIS IS THE GAME'S OWN ARITHMETIC (FUN_00050fc8). It is inverted from the
        write head, so index 1 is the NEWEST line — reading it in ascending index order
        gives the backlog newest-first, which is how the screen draws it.
        """
        if index == 0 or self.write_head == 0:
            return None
        i = (self.write_head - (index - 1)) - 1
        if i < 0 or i >= self.capacity:
            return None
        return self.log_base + i * ST

    def line(self, index: int):
        """Return (speaker_or_None, text) for a log index."""
        a = self.entry_addr(index)
        if a is None:
            return None, None
        # ⛔ THE FIELDS ARE FIXED-WIDTH AND NUL-PADDED. Split at the NUL BEFORE cleaning,
        # or the padding survives into the output and the string is unreadable.
        name_raw = self.d[a - RAM_BASE + OFF_NAME: a - RAM_BASE + OFF_NAME + 0x28].split(b"\x00")[0]
        text_raw = self.d[a - RAM_BASE + OFF_TEXT: a - RAM_BASE + OFF_TEXT + 0x60].split(b"\x00")[0]
        # ⛔ An entry may carry EITHER a name plate or the text itself (narration). The
        # name field is short and the text field is the spoken line; when the game has no
        # name plate the text starts in the name field instead. Pick whichever is longer.
        n = clean(name_raw)
        t = clean(text_raw)
        if not t and n:
            return None, n
        if len(n) > len(t):
            return None, n
        return (n or None), t

    # ---- reporting --------------------------------------------------------
    def report(self, count=12, oldest=False):
        print(f"dump         : {self.path}")
        print(f"log base     : 0x{self.log_base:08X}")
        print(f"write head   : {self.write_head}  entries logged")
        print(f"config       : 0x{self.config:08X}   capacity {self.capacity} x 0x{ST:X}")
        print()
        idxs = range(1, min(count, self.write_head) + 1)
        if oldest:
            idxs = range(max(1, self.write_head - count + 1), self.write_head + 1)
        for i in idxs:
            who, text = self.line(i)
            if text is None:
                print(f"  [{i:3d}] (no entry)")
                continue
            tag = f"{who}: " if who else ""
            print(f"  [{i:3d}] {tag}{text[:88]}")

    def speak_last(self):
        who, text = self.line(1)
        if text is None:
            print("(no dialogue logged yet)")
            return
        print(f"{who + ': ' if who else ''}{text}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--lines", type=int, default=12)
    ap.add_argument("--oldest", action="store_true", help="show the oldest retained entries")
    ap.add_argument("--speak-last", action="store_true", help="print just the newest line")
    args = ap.parse_args()

    log = Log(args.dump)
    if args.speak_last:
        log.speak_last()
    else:
        log.report(args.lines, args.oldest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
