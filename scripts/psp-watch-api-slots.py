#!/usr/bin/env python3
"""psp-watch-api-slots.py -- READ-watch the API table slots: this is the dynamic answer to the
                             static search that failed.

WHY THIS IS THE RIGHT MOVE (doc sections 89-93)
    Five static searches for the referrers of the pad API table came back empty:
      s73  the menu never names the pad static          -> 0 of 21
      s89  the table slots have no pointer holders      -> 0
      s90  code scalars/refs (loose filter)             -> 0 real (filter was invalid)
      s92  slot 0x08BA46E8 reserved across 7 screens    -> negative
      s93  code scalars/refs WITH the lw/sw MIPS idiom  -> 0 real

    s93 concluded the base is never materialised in code -- the table is ZERO IN THE ELF FILE and is filled
    and read via a COMPUTED base+index pointer, so no slot address ever appears as an immediate, scalar, or
    stored pointer.

    A READ WATCHPOINT DOES NOT CARE HOW THE ADDRESS WAS COMPUTED. Setting one on the API slots catches any
    instruction that loads from them, whether the address came from a literal, a pointer, or base+index
    arithmetic. This is the dynamic inverse of the failed static search, and it uses an instrument already
    proven in sections 78, 81 and 87.

WHAT IT DOES
    1. reads the table window so the slots are known;
    2. arms READ watchpoints over a run of API slots (default the query block 0x08BA46BC..0x08BA4700);
    3. collects hit PCs (arm -> read PC -> clear -> resume -> re-arm), classifying each into a function;
    4. reports who READS the API table -- which is the input-API consumer set.

    Liveness is asserted before and after via cpu.status (NOT game.status -- that has no ticks field; see s92).
    If the CPU is frozen or stepping the run reports it and does not fabricate a verdict.

USAGE
    python scripts/psp-watch-api-slots.py --lo 0x08BA46BC --size 0x48 --hits 30
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x08804000
SEG_END = BASE + 0x003A6860

# function boundaries from the reports in this project (entry, size), for classification
KNOWN = {
    0x000F6694: "FUN_000f6694 (pad edge processor)",
    0x000F67C0: "FUN_000f67c0 (bit-test predicate)",
    0x000F67DC: "FUN_000f67dc",
    0x000F67F8: "FUN_000f67f8",
    0x000F6814: "FUN_000f6814",
    0x000F6830: "FUN_000f6830",
    0x000F6FC8: "FUN_000f6fc8 (stores converted state)",
    0x000F7028: "FUN_000f7028 (hands bits on)",
    0x000F70C4: "FUN_000f70c4 (raw->logical translator)",
    0x000F7138: "FUN_000f7138 (reads raw button word)",
    0x000F71C8: "FUN_000f71c8 (guarded INPUT QUERY)",
    0x000F7224: "FUN_000f7224",
    0x000F7280: "FUN_000f7280",
    0x000F72DC: "FUN_000f72dc",
    0x000F7338: "FUN_000f7338",
    0x0024932C: "FUN_0024932c (node-list manager)",
    0x0025468C: "FUN_0025468c (def lookup idx*0x1c)",
    0x0024AEE0: "FUN_0024aee0 (RENDER LOOP)",
    0x0024ADF8: "FUN_0024adf8 (menu sound loader)",
}


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def cpu_status(c, timeout=1.5):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 4242}))
    end = time.time() + timeout
    while time.time() < end:
        try:
            c.ws.settimeout(0.3)
            m = json.loads(c.ws.recv())
        except Exception:
            continue
        if m.get("event") == "cpu.status":
            return m
    return None


def liveness(c, label, secs=1.5):
    a = cpu_status(c)
    time.sleep(secs)
    b = cpu_status(c)
    ta = a.get("ticks") if a else None
    tb = b.get("ticks") if b else None
    d = (tb - ta) if (ta is not None and tb is not None) else None
    ok = d is not None and d > 0
    print("  %-7s ticks delta %.1fs: %-12s stepping=%s paused=%s -> %s"
          % (label, secs, d, (b or {}).get("stepping"), (b or {}).get("paused"),
             "EXECUTING" if ok else "FROZEN"))
    return ok


def classify(ram):
    v = ram - BASE
    if not (0 <= v < SEG_END):
        return "outside the ELF segment"
    best = None
    for ent, lab in KNOWN.items():
        if ent <= v:
            if best is None or ent > best[0]:
                best = (ent, lab)
    if best is None:
        return "vaddr 0x%08X (no known function below it)" % v
    off = v - best[0]
    if off < 0x400:
        return "%s @ vaddr 0x%08X (+0x%X)" % (best[1], v, off)
    return "vaddr 0x%08X (nearest known: %s, +0x%X -- probably a different function)" % (v, best[1], off)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08BA46BC)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x48)
    ap.add_argument("--hits", type=int, default=30)
    ap.add_argument("--button", default="")
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not liveness(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    raw = c.read(a.lo, a.size)
    print()
    print("=== 1. the watched API slots 0x%08X..0x%08X ===" % (a.lo, a.lo + a.size))
    for i in range(0, a.size, 8):
        v = struct.unpack_from("<I", raw, i)[0]
        w = struct.unpack_from("<I", raw, i + 4)[0]
        tag = classify(v) if (0x08804000 <= v < SEG_END) else ("zero" if v == 0 else "0x%08X" % v)
        print("   0x%08X = 0x%08X (+4=0x%08X)  %s" % (a.lo + i, v, w, tag))

    # clear any stale breakpoints
    for ev in ("memory.breakpoint.clear.all",):
        try:
            c.ws.send(json.dumps({"event": ev, "requestId": 1}))
            time.sleep(0.2)
        except Exception:
            pass

    print()
    print("=== 2. READ-watching 0x%08X size 0x%X ===" % (a.lo, a.size))
    hits = []
    tries = 0
    while len(hits) < a.hits and tries < 120:
        tries += 1
        try:
            c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 2,
                                  "address": a.lo, "size": a.size, "type": "read"}))
            time.sleep(0.35)
            if a.button:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 3,
                                        "button": a.button, "frames": 6}))
            time.sleep(0.45)
            st = cpu_status(c)
            pc = st.get("pc") if st else None
            step = st.get("stepping") if st else None
            if pc:
                hits.append((pc, step))
            c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 4}))
            c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 5}))
            time.sleep(0.12)
        except Exception as e:
            print("   err:", e)
            time.sleep(0.2)
    c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 9}))
    c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 10}))
    time.sleep(0.3)

    print("   tries %d  hits %d" % (tries, len(hits)))
    if not hits:
        print("   NO HITS on this range while the game ran.")
        print("   -> either the slots are not read during this state, or the range is wrong.")
    else:
        print()
        print("=== 3. WHO READS THE API TABLE SLOTS ===")
        from collections import Counter
        cnt = Counter(pc for pc, _ in hits)
        print("   distinct PCs: %d" % len(cnt))
        for pc, n in cnt.most_common():
            print("     0x%08X x%-3d  %s" % (pc, n, classify(pc)))
        print()
        print("   raw PCs: " + ", ".join("0x%08X" % pc for pc, _ in hits))

    print()
    print("=== liveness AFTER ===")
    live = liveness(c, "after")
    if not live:
        print("   NOTE: frozen/stepping at the end -- treat the hit set with caution.")

    c.close()
    pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
