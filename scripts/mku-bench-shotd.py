"""shotd.py: helper daemon for the interactive desktop session (the tool shell runs in session 0).
 - shot.req (a file stem)   -> C:\\Users\\Public\\<stem>.png via psp-shot.py, then <stem>.done
 - key.req  ("KEY secs")    -> focuses the PPSSPP window and holds that key (SendInput), then key.done
Keys are looked up by name: I J K L (PPSSPP's default analog stick), arrows, X Z A S V ...
"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

REQ_SHOT = r'C:\Users\Public\shot.req'
REQ_KEY = r'C:\Users\Public\key.req'
user32 = ctypes.windll.user32
VK = {'I': 0x49, 'J': 0x4A, 'K': 0x4B, 'L': 0x4C, 'UP': 0x26, 'DOWN': 0x28, 'LEFT': 0x25, 'RIGHT': 0x27,
      'X': 0x58, 'Z': 0x5A, 'A': 0x41, 'S': 0x53, 'V': 0x56, 'Q': 0x51, 'W': 0x57, 'SPACE': 0x20,
      'ENTER': 0x0D, 'ESC': 0x1B}


def find_ppsspp():
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        if user32.IsWindowVisible(h):
            n = user32.GetWindowTextLengthW(h)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(h, buf, n + 1)
            if 'PPSSPP' in buf.value:
                found.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def focus(h):
    fg = user32.GetForegroundWindow()
    cur = ctypes.windll.kernel32.GetCurrentThreadId()
    fgt = user32.GetWindowThreadProcessId(fg, None)
    user32.AttachThreadInput(fgt, cur, True)
    user32.ShowWindow(h, 9)
    user32.SetForegroundWindow(h)
    user32.AttachThreadInput(fgt, cur, False)
    time.sleep(0.15)


def key_event(vk, down):
    scan = user32.MapVirtualKeyW(vk, 0)
    flags = 0 if down else 2          # KEYEVENTF_KEYUP
    user32.keybd_event(vk, scan, flags, 0)


while True:
    try:
        if os.path.exists(REQ_SHOT):
            stem = open(REQ_SHOT).read().strip()
            os.remove(REQ_SHOT)
            if stem:
                subprocess.run([sys.executable, r'C:\Users\Public\psp-shot.py',
                                r'C:\Users\Public\%s.png' % stem, '--title', 'PPSSPP'], capture_output=True)
                open(r'C:\Users\Public\%s.done' % stem, 'w').write('ok')
        if os.path.exists(REQ_KEY):
            spec = open(REQ_KEY).read().split()
            if len(spec) < 2:
                time.sleep(0.05)
                continue
            os.remove(REQ_KEY)
            # "KEY[+KEY2] secs"
            keys, secs = spec[0].upper().split('+'), float(spec[1])
            h = find_ppsspp()
            if h:
                focus(h)
            vks = [VK[k] for k in keys]
            for v in vks:
                key_event(v, True)
            time.sleep(secs)
            for v in vks:
                key_event(v, False)
            open(r'C:\Users\Public\key.done', 'w').write('ok')
    except Exception as e:
        open(r'C:\Users\Public\shotd.err', 'a').write(repr(e) + '\n')
    time.sleep(0.05)
