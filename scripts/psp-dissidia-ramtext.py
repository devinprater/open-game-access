#!/usr/bin/env python3
"""psp-dissidia-ramtext.py -- find Dissidia's decoded text in RAM and compare it to the archive.

BREAKTHROUGH THIS EXPLOITS
    A whole-RAM scan found real English in Dissidia's RAM:
        @0x0AE59124  "Other players can see the information on your friend card during wireless play. Avoid entering perso"
        @0x0AE59206  "Other players can see the names of your artifacts during wireless play. Avoid entering personal info"
    That is friend_card.bin text, DECODED. Since the on-disk copies are not plaintext, the game must
    decode them at load time -- so RAM has the plaintext and the disk has the encoded form.

WHAT IT DOES
    1. scans RAM for menu/UI words to establish the full set of decoded strings and their addresses
    2. searches the archived files for the SAME string in plain form (to prove disk vs RAM differ)
    3. if a disk copy is found, aligns it with the RAM copy and reports the byte relationship

USAGE
    python scripts/psp-dissidia-ramtext.py --ram --words "Customize,Ability,Accessory,Item,Story"
    python scripts/psp-dissidia-ramtext.py --disk FILE --find "wireless play"
    python scripts/psp-dissidia-ramtext.py --compare --ram-addr 0x0AE59124 --disk FILE
"""
import argparse, base64, json, os, re, struct, sys

try:
    import websocket
except ImportError:
    websocket = None

RAM_BASE, RAM_SIZE = 0x08800000, 0x08000000


class Debugger:
    def __init__(self, url="ws://127.0.0.1:12345/debugger", timeout=25):
        self.ws = websocket.create_connection(url, timeout=timeout)
        self._id = 0

    def call(self, event, **params):
        self._id += 1
        m = {"event": event, "requestId": self._id}
        m.update(params)
        self.ws.send(json.dumps(m))
        return json.loads(self.ws.recv())

    def read(self, addr, size):
        r = self.call("memory.read", address=addr, size=size)
        b64 = r.get("base64") or ""
        try:
            return base64.b64decode(b64)
        except Exception:
            return b""

    def close(self):
        try:
            self.ws.close()
        except Exception:
            pass


def find_ascii(buf, needle):
    out = []
    nb = needle.encode()
    st = 0
    while True:
        i = buf.find(nb, st)
        if i < 0:
            break
        out.append(i)
        st = i + 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ram", action="store_true")
    ap.add_argument("--words", default="Customize,Ability,Accessory,Item,Story,Museum,Shop,"
                                      "Options,Friend Card,Party,Moogle,Select,Cancel,Start")
    ap.add_argument("--disk")
    ap.add_argument("--find")
    ap.add_argument("--compare", action="store_true")
    ap.add_argument("--ram-addr", type=lambda x: int(x, 0))
    ap.add_argument("--context", type=int, default=220)
    ap.add_argument("--chunk-mib", type=int, default=8)
    a = ap.parse_args()

    if a.disk and a.find:
        d = open(a.disk, "rb").read()
        hits = find_ascii(d, a.find)
        print("=== %s: %d plaintext hit(s) for %r ===" % (os.path.basename(a.disk), len(hits), a.find))
        for h in hits[:5]:
            print("   @%d  %r" % (h, d[max(0, h - 40):h + 120]))
        if not hits:
            # try UTF-16LE
            h2 = find_ascii(d, "\x00".join(a.find))
            print("   UTF-16LE hits: %d" % len(h2))
        return

    dbg = Debugger()
    print("connected")

    if a.compare and a.ram_addr is not None:
        b = dbg.read(a.ram_addr, a.context)
        print("=== RAM @0x%08X ===" % a.ram_addr)
        print("   %r" % b)
        if a.disk:
            d = open(a.disk, "rb").read()
            # take a distinctive slice of the RAM text and look for it in the file
            probe = re.sub(rb"[^\x20-\x7e]", b"", b)[12:40]
            print("   probing disk for: %r" % probe)
            hits = find_ascii(d, probe.decode("latin1", "ignore"))
            print("   disk hits: %d %s" % (len(hits), hits[:3]))
        dbg.close()
        return

    if a.ram:
        words = [w.strip() for w in a.words.split(",") if w.strip()]
        print("scanning user RAM for %d word(s)..." % len(words))
        # read the whole user region once, in chunks, and search
        hits = {w: [] for w in words}
        pos = 0
        while pos < RAM_SIZE:
            ln = min(a.chunk_mib << 20, RAM_SIZE - pos)
            b = dbg.read(RAM_BASE + pos, ln)
            if b:
                for w in words:
                    for i in find_ascii(b, w):
                        if len(hits[w]) < 6:
                            hits[w].append((RAM_BASE + pos + i, b[i:i + 80]))
            pos += ln
            print("   ...0x%08X" % (RAM_BASE + pos), end="\r", flush=True)
        print()
        print("\n=== RAM hits by word ===")
        for w in words:
            h = hits[w]
            if h:
                print("  %-12s %d hit(s)" % (w, len(h)))
                for addr, ctx in h[:3]:
                    txt = "".join(chr(c) if 32 <= c < 127 else "." for c in ctx[:72])
                    print("      @0x%08X  %s" % (addr, txt))
            else:
                print("  %-12s none" % w)
    dbg.close()


if __name__ == "__main__":
    main()
