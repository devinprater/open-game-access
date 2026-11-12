#!/usr/bin/env python3
"""drive.py: ONE connection from a fresh PPSSPP launch through the profile kode, Konquest entry
and the dojo drills. Optional arg 'shots' saves screenshots during the attack drill.
Honest scope: it logs what it sees; it does not claim the tutorial passed unless the log says so."""
import pathlib
import sys, time, json, struct, subprocess, importlib.util
spec = importlib.util.spec_from_file_location('c', str(pathlib.Path(__file__).resolve().parent / 'psp-ppsspp-client.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
d = c.Debugger(timeout=6)
SHOTS = 'shots' in sys.argv
TEXT, COL, SCENE = 0x09178568, 0x09178554, 0x09178D30
t0 = time.time()


def log(m):
    print('%6.1f %s' % (time.time() - t0, m), flush=True)


_tk = [0]


def rd(a, n):
    """memory.read matched by ticket, so a late reply to an earlier request is never mistaken
    for this one (unticketed matching returned garbage floats under the button traffic)."""
    for _ in range(4):
        _tk[0] += 1
        tk = 'r%d' % _tk[0]
        d.ws.send(json.dumps({"event": "memory.read", "address": a, "size": n, "ticket": tk}))
        t_end = time.time() + 3
        while time.time() < t_end:
            try:
                raw = d.ws.recv()
            except Exception:
                break
            if ('"ticket":"%s"' % tk) not in raw:
                continue
            try:
                j = json.loads(raw)
            except Exception:
                break
            if 'base64' in j:
                import base64
                b = base64.b64decode(j['base64'])
                if len(b) == n:
                    return b
            break
        time.sleep(0.1)
    return bytes()


def u32(a):
    r = rd(a, 4)
    return struct.unpack('<I', r)[0] if len(r) == 4 else None


def btn(b, down):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: down}}))


def tap(b, down=0.1, up=0.4):
    btn(b, True); time.sleep(down); btn(b, False); time.sleep(up)


def dtap(b):
    btn(b, True); time.sleep(0.045); btn(b, False); time.sleep(0.055)
    btn(b, True); time.sleep(0.045); btn(b, False); time.sleep(0.5)


def shot(n):
    import os
    base = '/mnt/c/Users/Public/'
    for ext in ('done', 'png'):
        try:
            os.remove(base + '%s.%s' % (n, ext))
        except OSError:
            pass
    open(base + 'shot.req', 'w').write(n)
    for _ in range(100):
        if os.path.exists(base + '%s.done' % n):
            break
        time.sleep(0.1)


if 'noboot' not in sys.argv:
    # ---- boot to the main menu ------------------------------------------------------------
    time.sleep(30)
    for i in range(40):
        if u32(0x0911F868):
            break
        tap('start', 0.1, 2.5)
    log('main menu holder=%X' % (u32(0x0911F868) or 0))
    time.sleep(2)
    # ---- profile kode, then Konquest -------------------------------------------------------
    tap('ltrigger', 0.1, 1.5)
    for b in ['cross', 'circle', 'triangle', 'square', 'cross', 'circle']:
        tap(b)
    time.sleep(2)
    for b in ['down', 'down', 'down', 'cross']:
        tap(b)
    time.sleep(8)
    log('konquest flags %s %s' % (u32(0x08AF69C8), u32(0x08B123DC)))


# ---- the drills ---------------------------------------------------------------------------
last = None
attempts = 0
deadline = time.time() + float(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else time.time() + 600
while time.time() < deadline:
    col, scene = u32(COL), u32(SCENE)
    raw = rd(TEXT, 200)
    if col is None or scene is None or not raw:
        time.sleep(0.3)
        continue
    txt = raw.split(b'\0')[0].decode('latin1', 'replace')
    kind = 'DLG' if col == 0xC8C8C8FF else 'OBJ' if col == 0xFF4848FF else 'col=%X' % col
    key = (kind, txt, bool(scene))
    if key != last:
        log('%s%s: %s' % (kind, ' up' if scene else '', txt))
        last = key
    low = txt.lower()
    if scene and col in (0xC8C8C8FF, 0xFF4848FF):
        time.sleep(0.5)
        tap('cross', 0.1, 0.3)
        continue
    if col == 0xFF4848FF:
        if 'step toward' in low:
            for b in ['right'] * 3 + ['left'] * 3:
                tap(b, 0.15, 0.6)
            log('did steps')
        elif 'backward dash' in low:
            for _ in range(3):
                dtap('left')
            log('did backward dashes')
        elif 'basic attacks' in low or 'different' in low:
            attempts += 1
            import itertools, struct as _st
            perms = list(itertools.permutations(['square', 'triangle', 'cross', 'circle']))
            order = perms[(attempts - 1) * 5 % len(perms)] if 'diag' in sys.argv else                 ('square', 'triangle', 'circle', 'cross')

            def hp2():
                r = rd(0x08D92A24, 4)
                return _st.unpack('<f', r)[0] if len(r) == 4 else -1.0
            for _ in range(3):
                tap('right', 0.12, 0.25)
            log('attempt %d order %s start hp2=%.3f' % (attempts, order, hp2()))
            for b in order:
                before = hp2()
                tap(b, 0.08, 0.7)
                after = hp2()
                log('  %-8s hp2 %.3f -> %.3f %s' % (b, before, after, 'HIT' if after < before - 1e-4 else 'no damage'))
            shot('diag_%d' % attempts)
            log('did four attacks (attempt %d)' % attempts)
            time.sleep(1.0)
        else:
            ctx = dict(tap=tap, btn=btn, dtap=dtap, rd=rd, u32=u32, log=log, shot=shot, time=time,
                       struct=struct, low=low, attempts=attempts)
            try:
                ns = dict(ctx)
                exec(open('/home/devin/mku-bench/handlers.py').read(), ns)
                handled = ns['handle'](low, ctx)
            except Exception as e:
                log('handlers.py error: %r' % (e,))
                handled = False
            if not handled:
                log('UNHANDLED objective')
                time.sleep(2)
    time.sleep(0.4)
log('done')
d.close()
