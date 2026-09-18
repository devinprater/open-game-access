#!/usr/bin/env python3
"""psp-cursor-hunt.py -- the CHAINED test: find a live control, then probe the nodes immediately.

WHY THIS EXACT FORM (doc section 53)
    Section 53 found that `circle` and `triangle` DO move a static screen (the previous control lists
    omitted the face buttons), but every attempt to follow up voided because reaching the screen again
    landed somewhere else. The specified-but-never-run form is:

        one process, no re-navigation between steps
          1. liveness before
          2. hunt for a STATIC screen (press control, check staticness after each move)
          3. on the static screen, scan ALL twelve controls, and for each record:
                 display diff, and whether the screen is STILL STATIC afterwards
          4. take the FIRST control that both moved the display AND left it static
          5. IMMEDIATELY run the node-field diff with that control, 8-12 presses
          6. liveness after; verdict

    The "still static afterwards" test is what separates a UI transition from scene motion, and step 4
    uses it as the selection criterion. Steps 3 and 5 happen on the same screen in the same process, so
    there is no drift gap and no re-navigation.

WHAT A RESULT LOOKS LIKE
    * a control that moves the display and leaves it static, AND node fields that change with it
      -> the node list IS the selection state (cursor found);
    * such a control but NO node field changes
      -> the selection is not in the node list, which is the last unverified structural negative.

USAGE
    python scripts/psp-cursor-hunt.py
"""
import importlib.util
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
MGR_PTR = 0x08804000 + 0x00397770
CONTROLS = ["down", "up", "left", "right", "l", "r",
            "circle", "cross", "triangle", "square", "start", "select"]
STATIC_MAX = 2000
NODE_FIELDS = [("i0C", 0x0C, "i"), ("b14", 0x14, "b"), ("b17", 0x17, "b"),
               ("w20", 0x20, "H"), ("i28", 0x28, "i"), ("i3C", 0x3C, "i")]


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def drain(ws, sec):
    out = []
    end = time.time() + sec
    while time.time() < end:
        try:
            ws.settimeout(max(0.02, end - time.time()))
            out.append(json.loads(ws.recv()))
        except Exception:
            pass
    return out


def cpu(c):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(c.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def live(c, tag):
    a = cpu(c); time.sleep(1.4); b = cpu(c)
    if not a or not b:
        print("  %-6s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def shot(tag):
    q = os.path.join(TMP, "ch-" + tag + ".png")
    try:
        if os.path.exists(q):
            os.remove(q)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
    return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None


def diff(a, b):
    from PIL import Image
    import numpy as np
    if not a or not b:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(np.int16)
    y = np.asarray(Image.open(b).convert("RGB")).astype(np.int16)
    return int((np.abs(x - y).max(axis=2) > 16).sum())


def manager(c):
    raw = c.read(MGR_PTR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    h = c.read(mgr, 0x40)
    if not h or len(h) < 0x40:
        return mgr, 0, []
    head = struct.unpack_from("<I", h, 0x28)[0]
    nodes = []
    p = head
    seen = set()
    while p and p not in seen and len(nodes) < 200 and 0x08800000 <= p < 0x0A000000:
        seen.add(p)
        nb = c.read(p, 0x40)
        if not nb or len(nb) < 0x40:
            break
        rec = {"addr": p}
        for name, off, kind in NODE_FIELDS:
            rec[name] = nb[off] if kind == "b" else struct.unpack_from("<" + kind, nb, off)[0]
        nodes.append(rec)
        p = struct.unpack_from("<i", nb, 0x24)[0]
        if p == head:
            break
    return mgr, head, nodes


def press(pad, ctl, rid):
    pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": rid,
                            "button": ctl, "frames": 12}))
    time.sleep(1.4)


def main():
    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator frozen.")
        c.close(); pad.close()
        return 2

    # ---- step 2: hunt for a static screen ----
    print()
    print("=== hunting for a STATIC screen ===")
    static_ok = False
    nav = ["l", "r", "start", "select", "circle", "triangle", "cross", "square"]
    for i in range(16):
        ctl = nav[i % len(nav)]
        press(pad, ctl, 100 + i)
        a = shot("h%d" % i); time.sleep(2.0); b = shot("h%db" % i)
        d = diff(a, b)
        print("   move %-2d (%-8s) no-press diff: %s" % (i + 1, ctl, d))
        if d is not None and d <= STATIC_MAX:
            static_ok = True
            break
    if not static_ok:
        print("   no static screen found in 16 moves")
        c.close(); pad.close()
        return 1

    print("   STATIC screen reached.")
    mgr, head, base_nodes = manager(c)
    print("   manager 0x%08X  nodes %d" % (mgr, len(base_nodes)))

    # ---- step 3: scan all twelve controls, requiring "still static after" ----
    print()
    print("=== scanning all twelve controls on this static screen ===")
    prev = shot("s0")
    winner = None
    for j, ctl in enumerate(CONTROLS):
        press(pad, ctl, 300 + j)
        cur = shot("c%d" % j)
        moved = diff(prev, cur)
        time.sleep(1.6)
        after = shot("c%db" % j)
        settled = diff(cur, after)
        verdict = ""
        if moved:
            if settled is not None and settled <= STATIC_MAX:
                verdict = "<== MOVED and SETTLED (UI transition)"
                if winner is None:
                    winner = ctl
            else:
                verdict = "moved but still changing (scene motion)"
        print("   %-9s -> %9s px   settled diff %9s  %s" % (ctl, moved, settled, verdict))
        prev = cur

    print()
    if not winner:
        print("NO control both moved the display and settled. Cannot exercise the selection here.")
        live(c, "after")
        c.close(); pad.close()
        return 1
    print("LIVE CONTROL SELECTED: %s" % winner)

    # ---- step 5: immediate node-field diff with the winning control ----
    print()
    print("=== node-field diff with '%s' (same screen, same process) ===" % winner)
    _, _, n0 = manager(c)
    series = [n0]
    p_prev = shot("n0")
    moved = 0
    for k in range(8):
        press(pad, winner, 500 + k)
        _, _, nk = manager(c)
        series.append(nk)
        cur = shot("n%d" % k)
        if diff(p_prev, cur):
            moved += 1
        p_prev = cur

    print("   presses that moved the display: %d/8" % moved)
    print("   node counts: %s" % [len(s) for s in series])
    width = min(len(s) for s in series)
    found = 0
    for i in range(width):
        for name, off, kind in NODE_FIELDS:
            vals = [series[k][i][name] for k in range(len(series))]
            if len(set(vals)) > 1:
                found += 1
                print("   node[%d] 0x%08X %-4s %s"
                      % (i, series[0][i]["addr"], name, vals))
    if found == 0:
        print("   (no node field changed)")

    print()
    print("=== liveness AFTER ===")
    ok = live(c, "after")
    print()
    if moved == 0:
        print("VERDICT: VOID -- the selected control stopped moving the display.")
    elif not ok:
        print("VERDICT: SUSPECT -- emulator froze during the run.")
    elif found:
        print("VERDICT: NODE FIELDS TRACK THE SELECTION -- inspect the fields listed above.")
    else:
        print("VERDICT: TRUSTWORTHY NEGATIVE -- '%s' moves the display and settles, yet no node" % winner)
        print("         field changes, so the selection is NOT stored in the node list.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
