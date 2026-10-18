#!/usr/bin/env python3
"""Pokemon Stadium 1 desktop harness: talk to RetroArch's MCP server.

RetroArch is launched separately (see run-ra.sh). This script only drives it.

Usage:
  python ps.py status
  python ps.py frames 120
  python ps.py press a [frames]
  python ps.py shot
  python ps.py read 80075768 16
  python ps.py u32 80075768
  python ps.py dump <file>
  python ps.py state
  python ps.py save 1 | load 1
"""
import json
import os
import sys
import time
import urllib.request

URL = "http://127.0.0.1:54123/mcp"
TOKEN = "ogatesttoken"

# Stadium 1 (USA) Rev 2. The decompilation targets 1.0 and its addresses are
# shifted by +0x100 here; these are the live Rev 2 addresses, measured.
STATE = 0x800757A8
LAST_STATE = 0x800757AC

STATE_NAMES = {
    0x01: "N64 logo and intro",
    0x02: "title screen",
    0x03: "unused N64DD boot",
    0x04: "menu select",
    0x10: "area select",
    0x11: "gallery",
    0x12: "event battle",
    0x13: "options",
    0x20: "Pokemon Stadium menu",
    0x21: "free battle",
    0x22: "VS Mewtwo",
    0x23: "Kids Club",
    0x24: "Victory Palace",
    0x25: "Pokemon Lab",
    0x26: "GB Tower",
    0x27: "Gym Leader Castle",
    0x28: "Battle Now, 1 player",
    0x29: "Battle Now, 2 players",
    0x2A: "battle from event",
    0x40: "fast N64 logo",
    0x80: "stubbed debug menu",
    0x81: "fast battle",
    0x82: "Kids Club title",
}


def _post(payload):
    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        URL,
        data=body,
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "Authorization": "Bearer " + TOKEN,
        },
    )
    raw = urllib.request.urlopen(req, timeout=180).read().decode("utf-8", "replace")
    if raw.lstrip().startswith("event:") or "\ndata:" in raw[:40]:
        raw = "".join(line[5:].strip() for line in raw.splitlines() if line.startswith("data:"))
    return json.loads(raw)


def call(name, args=None):
    res = _post({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                 "params": {"name": name, "arguments": args or {}}})
    try:
        return res["result"]["content"][0]["text"]
    except Exception:
        return json.dumps(res)


def status():
    return call("GET_STATUS").strip()


def frames(n):
    return call("RUN_FRAMES", {"count": int(n)}).strip()


def press(button, hold=6, settle=24):
    """Hold a RetroPad button for `hold` frames, release, settle."""
    call("SET_INPUT", {"player": 1, "control": button, "value": 1})
    frames(hold)
    call("SET_INPUT", {"player": 1, "control": button, "value": 0})
    if settle:
        frames(settle)


def read_bytes(addr, n):
    text = call("READ_CORE_MEMORY", {"address": format(addr, "X"), "bytes": int(n)})
    parts = text.split()
    if len(parts) < 3 or parts[2] == "ERROR":
        raise RuntimeError(text)
    return bytes(int(x, 16) for x in parts[2:])


def read_u32(addr):
    b = read_bytes(addr, 4)
    return int.from_bytes(b, "little")


def dump(path, size=0x400000, chunk=0x4000):
    out = bytearray()
    for off in range(0, size, chunk):
        out += read_bytes(0x80000000 + off, chunk)
    with open(path, "wb") as f:
        f.write(out)
    return len(out)


def state():
    cur = read_u32(STATE)
    last = read_u32(LAST_STATE)
    return cur, last, STATE_NAMES.get(cur, "unknown screen")


def shot():
    res = _post({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                 "params": {"name": "SCREENSHOT", "arguments": {}}})
    # The tool returns an image; RetroArch also writes a file. Find the newest.
    import glob
    root = r"C:\Users\Devin Prater\Downloads\ra-mcp-test\RASETUP\screenshots"
    files = sorted(glob.glob(os.path.join(root, "*.png")), key=os.path.getmtime)
    return files[-1] if files else "(no screenshot file)"


def state_slot(action, slot):
    name = "SAVE_STATE_SLOT" if action == "save" else "LOAD_STATE_SLOT"
    return call(name, {"slot": int(slot)}).strip()


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "state"
    if cmd == "status":
        print(status())
    elif cmd == "frames":
        print(frames(argv[2]))
    elif cmd == "press":
        press(argv[2], int(argv[3]) if len(argv) > 3 else 6,
              int(argv[4]) if len(argv) > 4 else 24)
        print("pressed", argv[2], state())
    elif cmd == "shot":
        print(shot())
    elif cmd == "read":
        b = read_bytes(int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 16)
        print(" ".join(f"{x:02X}" for x in b))
    elif cmd == "u32":
        print(f"{read_u32(int(argv[2], 16)):08X}")
    elif cmd == "dump":
        print("wrote", dump(argv[2]), "bytes")
    elif cmd == "state":
        cur, last, name = state()
        print(f"state=0x{cur:02X} ({name}) last=0x{last:02X}")
    elif cmd in ("save", "load"):
        print(state_slot(cmd, argv[2]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
