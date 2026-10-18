#!/usr/bin/env python3
"""Pokemon Stadium 1 reader, desktop prototype.

Reads the live game through RetroArch's MCP server and speaks screen and menu
changes through NVDA (falls back to console output if NVDA is unavailable).

Run beside a running RetroArch instance:
    python reader.py

It never sends input. The player drives the game; the reader only observes.
"""
import ctypes
import struct
import sys
import time

import ps

# Measured on Pokemon Stadium (USA) Rev 2, 2026-10-18.
STATE_ADDR = 0x800757A8
AREA_CURSOR_ADDR = 0x80117EFA          # s16 inside fragment37's overlay
CUP_CURSOR_ADDR = 0x8008157C           # u32, cup-select screen

SCREENS = {
    0x01: "Nintendo 64 logo",
    0x02: "Title screen. Press A or Start.",
    0x04: "Menu",
    0x10: "Area select",
    0x11: "Gallery",
    0x12: "Event battle",
    0x13: "Options",
    0x20: "Stadium cup select",
    0x21: "Free battle",
    0x22: "Versus Mewtwo",
    0x23: "Kids Club",
    0x24: "Victory Palace",
    0x25: "Pokemon Lab",
    0x26: "GB Tower",
    0x27: "Gym Leader Castle",
    0x28: "Battle Now, one player",
    0x29: "Battle Now, two players",
    0x2A: "Battle from event",
    0x40: "Fast logo",
    0x80: "Debug menu",
    0x81: "Fast battle",
    0x82: "Kids Club title",
}

# Index -> facility, from the game's own state table in fragment37.
FACILITIES = {
    0: "Stadium",
    1: "Free Battle",
    2: "Gym Leader Castle",
    3: "Versus Mewtwo",
    4: "Kids Club",
    5: "Victory Palace",
    6: "Pokemon Lab",
    7: "GB Tower",
    8: "Back to main menu",
}

# Measured values on the cup-select screen.
CUPS = {
    1: "Prime Cup, level 1 to 100",
    2: "Pika Cup, level 15 to 20",
    3: "Petit Cup, level 25 to 30",
    5: "Poke Cup, level 50 to 55",
}


class Speech:
    def __init__(self):
        self.dll = None
        for path in (r"C:\Program Files\FastSMRW\nvdaControllerClient64.dll",
                     r"C:\Program Files\NVDA\nvdaControllerClient64.dll"):
            try:
                d = ctypes.WinDLL(path)
                if d.nvdaController_testIfRunning() == 0:
                    d.nvdaController_speakText.argtypes = [ctypes.c_wchar_p]
                    self.dll = d
                    break
            except OSError:
                continue

    def say(self, text, interrupt=True):
        print(text, flush=True)
        if self.dll:
            if interrupt:
                self.dll.nvdaController_cancelSpeech()
            self.dll.nvdaController_speakText(text)


def read_state():
    b = ps.read_bytes(STATE_ADDR, 8)
    return struct.unpack("<2I", b)


def read_s16(addr):
    return struct.unpack("<h", ps.read_bytes(addr, 2))[0]


def read_u32(addr):
    return struct.unpack("<I", ps.read_bytes(addr, 4))[0]


def main():
    speech = Speech()
    speech.say("Pokemon Stadium reader ready.")
    last_state = None
    last_item = None
    pending_state = None
    state_hits = 0
    pending_item = None
    item_hits = 0
    while True:
        try:
            cur, last = read_state()
        except Exception as exc:
            time.sleep(0.5)
            continue

        # Debounce: a value must read the same twice before it is spoken, so a
        # one-poll transient during a screen load cannot become an announcement.
        if cur == pending_state:
            state_hits += 1
        else:
            pending_state, state_hits = cur, 1

        if state_hits == 2 and cur != last_state:
            speech.say(SCREENS.get(cur, f"Unknown screen {cur:#x}"))
            last_state = cur
            last_item = None

        item = None
        if cur == 0x10:
            v = read_s16(AREA_CURSOR_ADDR)
            if 0 <= v <= 8:
                item = FACILITIES.get(v)
        elif cur == 0x20:
            raw = ps.read_bytes(CUP_CURSOR_ADDR, 8)
            v, nxt = struct.unpack("<2I", raw)
            # The cup cursor is followed by a pointer into RDRAM on that screen.
            # Other Stadium sub-screens reuse this heap area, so without the
            # guard a stale word reads as a cup name.
            if 0x80000000 <= nxt < 0x80800000:
                item = CUPS.get(v)

        if item == pending_item:
            item_hits += 1
        else:
            pending_item, item_hits = item, 1

        if item is not None and item_hits == 4 and item != last_item:
            speech.say(item)
            last_item = item

        time.sleep(0.12)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
