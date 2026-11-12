# exec'd inside drive.py's cmd channel. Provides pos(), stick(), calibrate(), goto().
import struct, json, math
D = rd.__globals__['d']


def pos():
    r = rd(0x08AED478, 12)
    return struct.unpack('<3f', r) if len(r) == 12 else None


def stick(x, y, secs):
    """x right+, y: +1 = stick DOWN on the PSP (screen coords). Sends analog for secs then releases."""
    D.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    D.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0.0, "y": 0.0}))
    time.sleep(0.4)


def calibrate(secs=0.8):
    """World displacement (dx, dz) per second for stick up / right."""
    out = {}
    for name, (sx, sy) in {'up': (0, -1), 'right': (1, 0)}.items():
        p0 = pos()
        stick(sx, sy, secs)
        p1 = pos()
        out[name] = ((p1[0] - p0[0]) / secs, (p1[2] - p0[2]) / secs)
    return out


def goto(tx, tz, tol=1.5, max_steps=30):
    cal = calibrate()
    log('calibration up=%s right=%s' % (cal['up'], cal['right']))
    # world delta = up_vec * (-sy) + right_vec * sx  (per second of full tilt)
    ux, uz = cal['up']
    rx, rz = cal['right']
    det = ux * rz - uz * rx
    if abs(det) < 1e-3:
        log('calibration degenerate')
        return
    for i in range(max_steps):
        p = pos()
        dx, dz = tx - p[0], tz - p[2]
        dist = math.hypot(dx, dz)
        if dist < tol:
            log('arrived (%.1f, %.1f) dist %.1f' % (p[0], p[2], dist))
            return
        # solve a*up + b*right = (dx, dz)
        a = (dx * rz - dz * rx) / det
        b = (ux * dz - uz * dx) / det
        n = math.hypot(a, b) or 1
        secs = min(1.2, max(0.25, dist / 7.0))
        stick(b / n, -a / n, secs)
    p = pos()
    log('goto ended at (%.1f, %.1f) target (%.1f, %.1f)' % (p[0], p[2], tx, tz))
