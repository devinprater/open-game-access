#!/usr/bin/env python3
"""psp-ppsspp-client.py -- a robust PPSSPP debugger client that survives asynchronous events.

THE PROBLEM IT SOLVES
    PPSSPP's debugger emits ASYNCHRONOUS BROADCAST EVENTS (e.g. `input.analog`, and a
    `memory.read` carrying `{"message":..., "level":...}`). A client that treats the next `ws.recv()`
    as its own reply will read a broadcast instead — the symptom is a `memory.read` whose payload is
    an analog-stick event, or an empty base64. Every scan can also desync and time out.

    This client matches responses by EVENT NAME and skips anything that is not the expected event.

USAGE as a library
    from psp_ppsspp_client import Debugger
    d = Debugger()
    print(d.status())
    data = d.read(0x0AE59000, 4096)      # base64-decoded bytes, async events skipped
    d.close()

USAGE as a tool
    python scripts/psp-ppsspp-client.py --status
    python scripts/psp-ppsspp-client.py --dump 0x0AE59000 --len 512
    python scripts/psp-ppsspp-client.py --find "Customize,Ability,Item" [--min-len 4]
    python scripts/ppsspp... --region 0x0AE00000:0x0AF00000
"""
import argparse, base64, json, sys, time

try:
    import websocket
except ImportError:
    sys.exit("need websocket-client: python -m pip install websocket-client")

URL = "ws://127.0.0.1:12345/debugger"


class Debugger:
    """Minimal PPSSPP debugger client. Filters asynchronous broadcasts by event name."""

    def __init__(self, url=URL, timeout=25):
        self.ws = websocket.create_connection(url, timeout=timeout)
        self._id = 0
        self.last_error = None

    def _call(self, event, expect=("base64",), **params):
        self._id += 1
        msg = {"event": event, "requestId": self._id}
        msg.update(params)
        self.ws.send(json.dumps(msg))
        # Read until we see OUR event carrying one of the expected payload keys.
        for _ in range(200):
            try:
                raw = self.ws.recv()
            except Exception:
                return None
            if not raw:
                continue
            try:
                r = json.loads(raw)
            except Exception:
                continue
            # !!! An "error" event is a REAL ANSWER, not noise. PPSSPP replies
            #     {"event":"error","message":"Invalid address","level":2} when the address is not
            #     currently mapped. Silently returning empty bytes for that is indistinguishable
            #     from "the address is mapped but zero", which corrupts every scan built on it.
            if r.get("event") == "error":
                self.last_error = r.get("message", "error")
                return r
            if r.get("event") != event:
                continue                     # a broadcast, not our reply
            if any(k in r for k in expect):
                return r
            if "message" in r or "level" in r:
                return r
        return None

    # ---- address-space discovery -------------------------------------------------------------
    def probe(self, address, size=16):
        """True if `address` is readable RIGHT NOW (an error event means not mapped)."""
        r = self._call("memory.read", address=address, size=size)
        if r is None:
            return None
        return r.get("event") == "memory.read" and "base64" in r

    def map_readable(self, lo=0x08800000, hi=0x0C000000, step=0x100000, size=16):
        """Return the list of readable (start, end) spans. PSP user RAM is only a window of this
        range and the mapped set CHANGES with game state, so never assume it."""
        spans = []
        cur = None
        a = lo
        while a < hi:
            ok = self.probe(a, size)
            if ok is True:
                if cur is None:
                    cur = [a, a + step]
                else:
                    cur[1] = a + step
            else:
                if cur is not None:
                    spans.append(tuple(cur))
                    cur = None
            a += step
        if cur is not None:
            spans.append(tuple(cur))
        return spans

    def read(self, address, size):
        r = self._call("memory.read", address=address, size=size)
        if not r or "base64" not in r:
            return b""
        try:
            return base64.b64decode(r["base64"])
        except Exception:
            return b""

    def read_u32(self, address, count=1):
        r = self._call("memory.read_u32", expect=("value",), address=address, count=count)
        return r.get("value") if r else None

    def status(self):
        r = self._call("game.status", expect=("game",))
        return r or {}

    def close(self):
        try:
            self.ws.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=URL)
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--dump")
    ap.add_argument("--len", type=int, default=512)
    ap.add_argument("--find")
    ap.add_argument("--min-len", type=int, default=5)
    ap.add_argument("--region", help="START:END (hex, e.g. 0x0AE00000:0x0AF00000)")
    ap.add_argument("--chunk-kib", type=int, default=256)
    ap.add_argument("--limit", type=int, default=60)
    a = ap.parse_args()

    d = Debugger(a.url)

    if a.status:
        print(json.dumps(d.status()))
        d.close()
        return

    if a.dump:
        addr = int(a.dump, 0)
        b = d.read(addr, a.len)
        print("=== RAM @0x%08X (%d bytes) ===" % (addr, len(b)))
        print("hex   : %s" % b[:64].hex())
        print("ascii : %r" % "".join(chr(c) if 32 <= c < 127 else "." for c in b[:160]))
        # UTF-16LE view too
        u = "".join(chr(b[i]) if i + 1 < len(b) and b[i + 1] == 0 and 32 <= b[i] < 127 else "."
                    for i in range(0, min(len(b), 320), 2))
        print("utf16 : %s" % u)
        d.close()
        return

    if a.find or a.region:
        if a.region:
            lo, hi = [int(x, 0) for x in a.region.split(":")]
        else:
            lo, hi = 0x08800000, 0x08800000 + 0x08000000
        words = [w.strip() for w in (a.find or "").split(",") if w.strip()]

        print("mapping readable memory 0x%08X..0x%08X ..." % (lo, hi))
        spans = d.map_readable(lo, hi, step=0x100000)
        if not spans:
            print("NO readable memory found in that range -- check the game is running.")
            d.close()
            return
        print("readable spans:")
        for s0, s1 in spans:
            print("   0x%08X .. 0x%08X  (%d MiB)" % (s0, s1, (s1 - s0) >> 20))
        print("scanning for %s" % words)
        hits = {w: [] for w in words}
        CH = a.chunk_kib << 10
        for span_lo, span_hi in spans:
          pos = span_lo
          while pos < span_hi:
            ln = min(CH, span_hi - pos)
            b = d.read(pos, ln)
            if b:
                for w in words:
                    if len(hits[w]) >= a.limit:
                        continue
                    nb = w.encode()
                    st = 0
                    while True:
                        i = b.find(nb, st)
                        if i < 0:
                            break
                        hits[w].append(pos + i)
                        st = i + 1
                        if len(hits[w]) >= a.limit:
                            break
            pos += ln
            print("   ...0x%08X" % pos, end="\r", flush=True)
        print()
        total = 0
        for w in words:
            h = hits[w]
            total += len(h)
            print("  %-14s %d" % (w, len(h)))
            for addr in h[:8]:
                ctx = d.read(addr, 90)
                txt = "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)
                print("      @0x%08X  %s" % (addr, txt))
        print("total hits: %d" % total)
        d.close()
        return

    print("nothing to do; try --status / --dump / --find")
    d.close()


if __name__ == "__main__":
    main()
