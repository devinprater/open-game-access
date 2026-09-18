#!/usr/bin/env python3
"""psp-dissidia-list.py -- walk the MENU MANAGER's item list and find what marks the selection.

WHY THIS MODEL
    decompile4 fixed the layout of the object the event thunks receive (DAT_00397770, RAM 0x08B9B770):
      FUN_0024932c walks a list from param_1+0x28 (next pointer at node+0x24), and a second list from
      param_1+0x34, and FUN_0024910c maintains them as a DOUBLY-LINKED list with a free pool at
      +0x1490/+0x1494, decrementing counters at param_1+0x30 and param_1+0x3c.
      FUN_0025468c treats a node as actionable when *(ushort*)(node+0x20) & 1, and resolves its
      definition at  base = *(int*)(table+0xc)  +  (short)*(node+0xc) * 0x1c, bounds-checked against
      *(int*)(table+0x2c) -- so node+0xc is an INDEX and table+0x2c is a COUNT.

    Those are exactly the fields a selection cursor would live among, and they have never been read
    ON A MENU: previous reads of DAT_00397770 were taken on scenes, where +0x28 was 0 (no list).

WHAT IT DOES
    Reads DAT_00397770, reports the list heads/counts, walks both lists printing each node's
    index(+0xc), flags(+0x14), debounce(+0x17), active(+0x20) and child(+0x3c) -- then optionally
    presses a button and repeats, so any field that tracks the highlight is visible as a change.

USAGE
    python scripts/psp-dissidia-list.py                      # one snapshot
    python scripts/psp-dissidia-list.py --press down --steps 3
"""
import argparse, importlib.util, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
MANAGER = 0x08B9B770          # RAM address of DAT_00397770 (base 0x08804000 + 0x00397770)


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def u16(b, off):
    return struct.unpack_from("<H", b, off)[0]


def u8(b, off):
    return b[off]


def s16(b, off):
    return struct.unpack_from("<h", b, off)[0]


def snapshot(d, label):
    """Read the manager and walk both lists. Returns a dict of everything observed."""
    hdr = d.read(MANAGER, 0x40)
    if not hdr or len(hdr) < 0x40:
        print("%s: manager unreadable" % label)
        return None
    head_a, tail_a, cnt_a = u32(hdr, 0x28), u32(hdr, 0x2c), u32(hdr, 0x30)
    head_b, tail_b, cnt_b = u32(hdr, 0x34), u32(hdr, 0x38), u32(hdr, 0x3c)
    print("%s  listA head=0x%08X tail=0x%08X count=%d   listB head=0x%08X tail=0x%08X count=%d"
          % (label, head_a, tail_a, cnt_a, head_b, tail_b, cnt_b))

    nodes = []
    for lname, head in (("A", head_a), ("B", head_b)):
        p = head
        seen = set()
        i = 0
        while p and p not in seen and i < 64:
            seen.add(p)
            nb = d.read(p, 0x40)
            if not nb or len(nb) < 0x40:
                print("    %s[%d] 0x%08X unreadable" % (lname, i, p))
                break
            rec = dict(list=lname, i=i, addr=p,
                       index=s16(nb, 0xc), flags14=u8(nb, 0x14), deb17=u8(nb, 0x17),
                       active20=u16(nb, 0x20), child3c=u32(nb, 0x3c), next24=u32(nb, 0x24))
            nodes.append(rec)
            print("    %s[%d] 0x%08X idx=%-4d flags=0x%02X deb=%d active=0x%04X child=0x%08X"
                  % (lname, i, p, rec["index"], rec["flags14"], rec["deb17"],
                     rec["active20"], rec["child3c"]))
            p = rec["next24"]
            i += 1
    return dict(hdr=hdr, nodes=nodes,
                counters=(cnt_a, cnt_b, head_a, head_b))


def press(d, button, frames=10, wait=1.1):
    d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":%d}'
              % (button, frames))
    time.sleep(wait)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default=None)
    ap.add_argument("--steps", type=int, default=3)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))
    print("manager @0x%08X" % MANAGER)
    print()

    snaps = [snapshot(d, "initial")]
    if a.press:
        for s in range(1, a.steps + 1):
            press(d, a.press)
            print()
            snaps.append(snapshot(d, "after %s #%d" % (a.press, s)))

    # report anything that differed between snapshots, field by field
    print()
    print("=== fields that changed across presses ===")
    valid = [s for s in snaps if s]
    if len(valid) < 2:
        print("   (need at least two snapshots)")
        d.close()
        return
    nch = 0
    for field in ("index", "flags14", "deb17", "active20", "child3c", "next24"):
        width = min(len(s["nodes"]) for s in valid)
        for i in range(width):
            vals = [s["nodes"][i][field] for s in valid]
            if len(set(vals)) > 1:
                nch += 1
                print("   node[%d].%-8s %s" % (i, field, " -> ".join("%X" % v if isinstance(v, int) else str(v) for v in vals)))
    for i, name in enumerate(("countA", "countB", "headA", "headB")):
        vals = [s["counters"][i] for s in valid]
        if len(set(vals)) > 1:
            nch += 1
            print("   %-9s %s" % (name, " -> ".join("0x%X" % v for v in vals)))
    if nch == 0:
        print("   (nothing changed)")
    d.close()


if __name__ == "__main__":
    main()
