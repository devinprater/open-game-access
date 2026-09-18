#!/usr/bin/env python3
"""psp-ram-utf16.py -- scan a running PSP game's RAM for readable text.

WHY THIS TOOL
    The project's proven route to text is to let the GAME do the decoding and then read the result out
    of RAM. Tag Team's story text was found as UTF-16LE in RAM after extraction attempts on disk
    stalled; the same move is the right first test here. If Dissidia's menu text exists in readable
    form in RAM, we get the text and, from its address, a target for the adapter -- no cipher work.

WHAT IT DOES
    Connects to PPSSPP's debugger WebSocket. NOTE the endpoint is /debugger, NOT the root:
    ws://127.0.0.1:12345/debugger.  The root serves PPSSPP's web file server (which answers with
    "Handshake status 200 OK" and lists the mounted ISOs). Reads bulk RAM with
    `memory.read {address, size}` (base64), and reports runs of:
        * UTF-16LE print runs (the encoding Tag Team used)
        * single-byte ASCII print runs
    ranked by length and grouped by region.

USAGE
    python scripts/psp-ram-utf16.py --list-regions
    python scripts/psp-ram-utf16.py --scan --min-len 12 [--top 40]
    python scripts/psp-ram-utf16.py --dump --at 0x08FC0764 --len 512
"""
import argparse, base64, json, re, socket, struct, sys, time

try:
    import websocket  # websocket-client
except ImportError:
    websocket = None


class Debugger:
    def __init__(self, url="ws://127.0.0.1:12345/debugger", timeout=20):
        if websocket is None:
            sys.exit("need websocket-client: python -m pip install websocket-client")
        self.ws = websocket.create_connection(url, timeout=timeout)
        self._id = 0

    def call(self, event, **params):
        self._id += 1
        msg = {"event": event, "requestId": self._id}
        msg.update(params)
        self.ws.send(json.dumps(msg))
        raw = self.ws.recv()
        return json.loads(raw)

    def read(self, address, size):
        r = self.call("memory.read", address=address, size=size)
        b64 = r.get("base64") or r.get("data") or ""
        if not b64:
            # some builds nest it
            for k in ("result", "response"):
                if isinstance(r.get(k), dict) and r[k].get("base64"):
                    b64 = r[k]["base64"]
        try:
            return base64.b64decode(b64)
        except Exception:
            return b""

    def base(self):
        try:
            r = self.call("game.status")
            return r
        except Exception:
            return {}

    def close(self):
        try:
            self.ws.close()
        except Exception:
            pass


REGIONS = [("user", 0x08800000, 0x08000000)]

# =====================================================================================
# !!! SUPERSEDED -- DO NOT USE FOR ADDRESS-CRITICAL WORK !!!
#
# The scan path below steps a chunk counter across the HARD-CODED region above, which runs
# PAST the mapped end (Dissidia's user RAM ends at 0x0A000000, not 0x08800000+0x08000000).
# A memory.read crossing the mapped end returns 0 bytes WHOLE rather than truncating, so the
# counter keeps advancing while reads are empty and later hits get attributed to a WRONG
# BASE ADDRESS. This is what produced a text hit reported at 0x0AE59124 when the true
# address was 0x09E59124.
#
# It also swallows the debugger's {"event":"error","message":"Invalid address"} reply, so
# "not mapped" looks identical to "mapped but empty".
#
# USE INSTEAD:  scripts/psp-ppsspp-client.py --find "<words>" --region 0x08800000:0x0C000000
#               scripts/psp-dissidia-textdump.py
# Both enumerate MAPPED spans before scanning and surface error replies (skill Rules 82-83).
# Kept only as a record of the bug.
# =====================================================================================


def utf16_runs(buf, minlen):
    out = []
    i = 0
    n = len(buf)
    while i < n - 1:
        j = i
        s = []
        while j < n - 1 and 0x20 <= buf[j] <= 0x7E and buf[j + 1] == 0:
            s.append(chr(buf[j])); j += 2
        if len(s) >= minlen:
            out.append((i, "".join(s)))
            i = j
        else:
            i += 1
    return out


def ascii_runs(buf, minlen):
    return [(m.start(), m.group().decode("latin1"))
            for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, buf)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="ws://127.0.0.1:12345/debugger")
    ap.add_argument("--list-regions", action="store_true")
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--min-len", type=int, default=12)
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--chunk-mib", type=int, default=4)
    ap.add_argument("--at", type=lambda x: int(x, 0))
    ap.add_argument("--len", type=int, default=512)
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args()

    d = Debugger(a.url)
    st = d.base()
    print("debugger connected; status: %s" % json.dumps(st)[:200])

    if a.dump and a.at is not None:
        b = d.read(a.at, a.len)
        print("=== RAM @0x%08X (%d bytes) ===" % (a.at, len(b)))
        print("hex : %s" % b[:64].hex())
        print("ascii: %r" % "".join(chr(c) if 32 <= c < 127 else "." for c in b[:120]))
        u = utf16_runs(b, 4)
        for off, s in u[:10]:
            print("  utf16 @+%d  %s" % (off, s))
        d.close()
        return

    if a.scan:
        total_u = total_a = 0
        rows = []
        for name, base, size in REGIONS:
            pos = 0
            n = size
            while pos < n:
                ln = min(a.chunk_mib << 20, n - pos)
                b = d.read(base + pos, ln)
                if not b:
                    pos += ln
                    continue
                for off, s in utf16_runs(b, a.min_len):
                    total_u += 1
                    rows.append(("u16", base + pos + off, s))
                for off, s in ascii_runs(b, a.min_len):
                    total_a += 1
                    rows.append(("ascii", base + pos + off, s))
                pos += ln
                print("   ...0x%08X" % (base + pos), end="\r", flush=True)
        print()
        print("found: %d UTF-16LE runs, %d ASCII runs" % (total_u, total_a))
        rows.sort(key=lambda r: -len(r[2]))
        print("\n=== longest %d ===" % a.top)
        for kind, addr, s in rows[:a.top]:
            print("  %-5s @0x%08X  %s" % (kind, addr, s[:100]))
    d.close()


if __name__ == "__main__":
    main()
