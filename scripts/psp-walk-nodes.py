#!/usr/bin/env python3
"""psp-walk-nodes.py -- walk a manager's item list and find the node field that marks the highlight.

WHY
    Doc section 39 follow-up: DAT_00397770+0 holds a heap pointer (0x08C08EB0) whose layout matches the
    manager header used by FUN_0024932c exactly (+0x20 sentinel -1, +0x24 count, +0x28/+0x2C list
    pointers, +0x30/+0x3C counts). Its HEADER fields proved completely static across 14 verified
    presses, which is expected: a list header holds head/tail/count, while a highlight is stored
    PER NODE.

    Node layout recovered from the decompiles:
        node+0x0C  definition / table pointer       (FUN_0025468c: iVar3 = param_1[3])
        node+0x14  flags byte                       (FUN_0024910c: (*byte*)(param_2+5) & 0x10)
        node+0x20  actionable flags (ushort)        (FUN_0025468c: *(ushort*)(node+0x20) & 1)
        node+0x24  next                             (FUN_0024910c)
        node+0x28  prev                             (FUN_0024910c)
        node+0x3C  child list head                  (FUN_0025468c loop)
    and manager+0x28 is the HEAD, manager+0x2C the tail (from the unlink logic in FUN_0024910c).

    So: walk from the head and diff every node's fields across a press. The field that moves is the
    highlight; if several nodes change, the marker is a per-node flag.

USAGE
    python scripts/psp-walk-nodes.py --mgr 0x08C08EB0 --press down --rounds 3
"""
import argparse
import importlib.util
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))

NODE_OFFS = (0x0C, 0x14, 0x17, 0x20, 0x24, 0x28, 0x3C)


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def read_word(d, a, kind="i"):
    b = d.read(a, 4)
    if not b or len(b) < 4:
        return None
    return struct.unpack("<" + kind, b)[0]


def walk(d, mgr):
    head = read_word(d, mgr + 0x28)
    tail = read_word(d, mgr + 0x2C)
    cnt = read_word(d, mgr + 0x30)
    nodes = []
    p = head
    seen = set()
    while p and p not in seen and len(nodes) < 80:
        if not (0x08800000 <= p < 0x0A000000):
            break
        seen.add(p)
        nb = d.read(p, 0x40)
        if not nb or len(nb) < 0x40:
            break
        rec = {"addr": p}
        for off in NODE_OFFS:
            if off == 0x14 or off == 0x17:
                rec["b%02X" % off] = nb[off]
            elif off == 0x20:
                rec["w20"] = struct.unpack_from("<H", nb, off)[0]
            else:
                rec["i%02X" % off] = struct.unpack_from("<i", nb, off)[0]
        nodes.append(rec)
        nxt = struct.unpack_from("<i", nb, 0x24)[0]
        p = nxt if nxt != head else 0
    return head, tail, cnt, nodes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mgr", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()

    pp = load_client()
    r = pp.Debugger()      # reads
    p = pp.Debugger()      # input -- SEPARATE connection (doc section 36)

    print("manager 0x%08X" % a.mgr)
    snaps = []
    h, t, c, nodes = walk(r, a.mgr)
    print("head=0x%08X tail=0x%08X count=%s nodes=%d" % (h or 0, t or 0, c, len(nodes)))
    print()
    for n in nodes[:24]:
        print("   0x%08X  def=%s flags14=0x%02X f17=%d w20=0x%04X next=0x%08X child=0x%08X"
              % (n["addr"], n.get("i0C"), n.get("b14", 0), n.get("b17", 0),
                 n.get("w20", 0), n.get("i24", 0), n.get("i3C", 0)))
    snaps.append(nodes)

    for i in range(a.rounds):
        p.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}' % a.press)
        time.sleep(1.2)
        _, _, _, nodes = walk(r, a.mgr)
        snaps.append(nodes)
        print("   -- after %s #%d: %d nodes" % (a.press, i + 1, len(nodes)))

    print()
    print("=== node fields that changed across presses ===")
    width = min(len(s) for s in snaps)
    keys = [k for k in snaps[0][0].keys() if k != "addr"]
    found = 0
    for i in range(width):
        for k in keys:
            vals = [s[i][k] for s in snaps]
            if len(set(vals)) > 1:
                found += 1
                print("   node[%d] 0x%08X  %-5s  %s"
                      % (i, snaps[0][i]["addr"], k, vals))
    if not found:
        print("   (no node field changed)")
    r.close(); p.close()


if __name__ == "__main__":
    sys.exit(main())
