#!/usr/bin/env python3
"""Pokemon Stadium 1 reader, desktop prototype.

Reads the live game through RetroArch's MCP server and speaks screen and menu
changes through NVDA (falls back to console output if NVDA is unavailable).

Run beside a running RetroArch instance:
    python reader.py

It never sends input. The player drives the game; the reader only observes.
"""
import ctypes
import json
import os
import struct
import sys
import time

import ps
from species import SPECIES

RENTALS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "rentals.json"),
                         encoding="utf-8"))
CUP_BY_VALUE = {1: "prime", 2: "pika", 3: "petit", 5: "poke"}

# Measured on Pokemon Stadium (USA) Rev 2, 2026-10-18.
STATE_ADDR = 0x800757A8
AREA_CURSOR_ADDR = 0x80117EFA          # s16 inside fragment37's overlay
CUP_CURSOR_ADDR = 0x8008157C           # u32, cup-select screen
FRAGMENT_TABLE = 0x800A58F0            # gFragments[], 8 bytes per entry
CUP_FRAGMENT = 87                      # cup-select screen fragment
TEAM_FRAGMENT = 88                     # the team-select fragment
TEAM_STRUCT_OFFSET = 0x168D2           # rental cursor struct inside that fragment

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


def team_struct():
    """Return the rental cursor struct address, or None when the screen is away."""
    base = read_u32(FRAGMENT_TABLE + TEAM_FRAGMENT * 8)
    if not (0x80000000 <= base < 0x80800000):
        return None
    return base + TEAM_STRUCT_OFFSET


def fragment_loaded(fragment_id):
    base = read_u32(FRAGMENT_TABLE + fragment_id * 8)
    return 0x80000000 <= base < 0x80800000


def rental_entry(struct_addr):
    """Return (index, species name, card_open) for the rental screen, or None.

    The list is 3 columns by 4 visible rows. Scrolling one row at the bottom
    adds one to the scroll byte, so the visible row is scroll + row.
    The byte at +8 is 1 on the grid and 0 while the stat card is open.
    """
    b = ps.read_bytes(struct_addr, 0x10)
    scroll = b[3]
    row = struct.unpack_from("<h", b, 0xA)[0]
    col = struct.unpack_from("<h", b, 0xC)[0]
    if not (0 <= row <= 3 and 0 <= col <= 2):
        return None
    index = (scroll + row) * 3 + col
    if not (0 <= index < len(SPECIES)):
        return None
    return index, SPECIES[index], b[8] == 0


def describe_rental(entry, index, cup, card_open):
    """Speakable text for the highlighted rental, from the extracted ROM table."""
    table = RENTALS.get(cup) or RENTALS["poke"]
    rec = table[index] if index < len(table) else None
    if rec is None:
        return entry
    if not card_open:
        return f"{rec['name']}, number {rec['dex']}"
    moves = rec["moves"]
    labels = ("Up", "Right", "Down", "Left")
    move_text = ". ".join(f"{labels[i]}: {moves[i]}" for i in range(4) if moves[i])
    return (f"{rec['name']}, level {rec['level']}. "
            f"HP {rec['hp']}, attack {rec['attack']}, defense {rec['defense']}, "
            f"speed {rec['speed']}, special {rec['special']}. {move_text}.")


def main():
    speech = Speech()
    speech.say("Pokemon Stadium reader ready.")
    last_state = None
    last_screen_name = None
    last_item = None
    current_cup = "poke"
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

        item = None
        screen_name = SCREENS.get(cur, f"Unknown screen {cur:#x}")
        if cur == 0x10:
            v = read_s16(AREA_CURSOR_ADDR)
            if 0 <= v <= 8:
                item = FACILITIES.get(v)
        elif cur == 0x20:
            st = team_struct()
            if st is not None:
                entry = rental_entry(st)
                if entry is not None:
                    index, name, card_open = entry
                    screen_name = "Pokemon detail card" if card_open else "Rental Pokemon list"
                    item = describe_rental(name, index, current_cup, card_open)
                else:
                    screen_name = "Stadium menu"
            elif fragment_loaded(CUP_FRAGMENT):
                screen_name = "Stadium cup select"
                raw = ps.read_bytes(CUP_CURSOR_ADDR, 8)
                v, nxt = struct.unpack("<2I", raw)
                # The cup cursor is followed by a pointer into RDRAM on that
                # screen. Other Stadium sub-screens reuse this heap area.
                if 0x80000000 <= nxt < 0x80800000:
                    item = CUPS.get(v)
                    if v in CUP_BY_VALUE:
                        current_cup = CUP_BY_VALUE[v]
            else:
                screen_name = "Stadium menu"

        if state_hits == 2 and screen_name != last_screen_name:
            speech.say(screen_name)
            last_state = cur
            last_screen_name = screen_name
            last_item = None
            # A sub-screen change inside state 0x20 must re-announce the item.
            if item is not None:
                pending_item, item_hits = item, 1

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
