import pathlib
exec(open(pathlib.Path(__file__).resolve().parent / "mku-bench-lib.py").read())
import math, sys
SUB = 0x09106354
OLD = 0x09151098 - 26 * 0x810 + 0x1C

def stick(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.15)

def me():
    p = struct.unpack("<3f", rd(0x08AED478, 12)); return p[0], p[2]
def him():
    p = struct.unpack("<3f", rd(OLD, 12)); return p[0], p[2]
def dist():
    a, b = me(), him(); return math.hypot(a[0] - b[0], a[1] - b[1])
def txt():
    return rd(SUB, 160).split(b"\0")[0].decode("latin1", "replace").strip()

dirs = [(math.cos(k * math.pi / 4), math.sin(k * math.pi / 4)) for k in range(8)]
t0 = time.time()
cur = None; since = time.time(); logged = None; presses = 0
target = float(sys.argv[1]) if len(sys.argv) > 1 else 1.8
total = float(sys.argv[2]) if len(sys.argv) > 2 else 120
best_dir = 0
lastx = 0
print("start dist %.1f" % dist(), flush=True)
while time.time() - t0 < total:
    s = txt(); now = time.time()
    if s != cur: cur = s; since = now
    elif s and s != logged and now - since > 0.7:
        logged = s
        print("%5.1f LINE: %s [B=%s]" % (now - t0, s, u32(0x08B123DC)), flush=True)
    if s and now - since > 1.6 and presses < 60:
        tap("cross", 0.1, 0.1); presses += 1; since = time.time()
    dnow = dist()
    if not s and dnow < 2.5 and now - lastx > 2.0:
        tap("cross", 0.1, 0.2); lastx = time.time(); print("%5.1f cross at %.1f" % (now - t0, dnow), flush=True)
    if dnow > target + 0.4 and not s:
        # try the last good direction, else search
        before = dnow
        for k in [best_dir] + [i for i in range(8) if i != best_dir]:
            stick(dirs[k][0], dirs[k][1], 0.35)
            after = dist()
            if after < before - 0.15:
                best_dir = k; break
            if after > before + 0.15:
                # went the wrong way; undo
                stick(-dirs[k][0], -dirs[k][1], 0.35)
            before = dist()
        print("%5.1f dist %.1f dir %d" % (now - t0, dist(), best_dir), flush=True)
    else:
        time.sleep(0.05)
print("done")
