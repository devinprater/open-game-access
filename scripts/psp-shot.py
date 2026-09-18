#!/usr/bin/env python
"""psp-shot.py — capture the PPSSPP window to a PNG. NO debugger socket needed.

WHY THIS EXISTS: the advance loop (`psp-tt-advance.mjs`) holds PPSSPP's single debugger client, so
it cannot screenshot from within its own process. But the screenshot path used elsewhere is
Windows `PrintWindow` + GetDIBits via ctypes, which talks to the WINDOW, not the socket. So
screenshots can be taken by a completely separate process WHILE a debugger client is attached.

That gap made an automated advance run unverifiable: it logged `#SHOT` markers but produced no
images. This script closes it.

Usage:
    python scripts/psp-shot.py out.png
    python scripts/psp-shot.py out.png --title PPSSPP
"""
import argparse
import ctypes
import sys
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

PW_RENDERFULLCONTENT = 0x00000002


def find_window(title_substr):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n == 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        if title_substr.lower() in buf.value.lower():
            found.append((hwnd, buf.value))
        return True

    user32.EnumWindows(cb, 0)
    return found


def grab(hwnd):
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right - rect.left, rect.bottom - rect.top
    if w <= 0 or h <= 0:
        return None, 0, 0

    hdc = user32.GetDC(hwnd)
    memdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(memdc, bmp)

    # PW_RENDERFULLCONTENT is required for GPU-rendered (DirectX) windows; without it the
    # captured image is blank/black.
    ok = user32.PrintWindow(hwnd, memdc, PW_RENDERFULLCONTENT)
    if not ok:
        ok = user32.PrintWindow(hwnd, memdc, 0)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
            ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
            ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
            ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
            ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
            ("biClrImportant", wintypes.DWORD),
        ]

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth = w
    bi.biHeight = -h          # negative => top-down rows
    bi.biPlanes = 1
    bi.biBitCount = 32
    bi.biCompression = 0

    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(hwnd, hdc)
    return buf.raw, w, h


def write_png(path, w, h, raw_rgba):
    """Write a PNG using ONLY the stdlib.

    WHY NOT PIL: capture is invoked as a subprocess from other scripts, and a bare `python.exe`
    on Windows resolves to whichever interpreter the PARENT's PATH puts first. When the parent is
    Hermes's own venv, that interpreter has no PIL, so the child died with
    ModuleNotFoundError before writing anything -- which surfaced as a silent "no file produced"
    and was misread as "the screen did not change". Encoding the PNG here removes the dependency,
    so capture works under any interpreter.
    """
    import struct
    import zlib

    stride = w * 4
    out = bytearray()
    for y in range(h):
        row = raw_rgba[y * stride:(y + 1) * stride]
        if len(row) != stride:
            row = row + b"\x00" * (stride - len(row))
        rgb = bytearray(w * 3)
        mv = memoryview(row)
        rgb[0::3] = mv[0::4]
        rgb[1::3] = mv[1::4]
        rgb[2::3] = mv[2::4]
        out.append(0)          # filter type 0 (None)
        out.extend(rgb)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8-bit, truecolour RGB
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(out), 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(png)


def save_rgb(path, w, h, raw_rgba, crop_rows=0):
    """Save the capture, preferring PIL when present and falling back to the stdlib writer."""
    if crop_rows:
        stride = w * 4
        raw_rgba = b"".join(raw_rgba[(y + crop_rows) * stride:(y + crop_rows + 1) * stride]
                            for y in range(h - crop_rows))
        h = h - crop_rows
    try:
        from PIL import Image
        im = Image.frombytes("RGBA", (w, h), raw_rgba).convert("RGB")
        im.save(path)
        return "PIL"
    except Exception:
        write_png(path, w, h, raw_rgba)
        return "stdlib"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--title", default="PPSSPP")
    ap.add_argument("--chrome", type=int, default=0, help="rows to crop off the top")
    args = ap.parse_args()

    wins = find_window(args.title)
    if not wins:
        print(f"ERROR: no visible window matching '{args.title}'")
        return 1
    hwnd, title = wins[0]
    raw, w, h = grab(hwnd)
    if raw is None:
        print("ERROR: zero-size window")
        return 1

    how = save_rgb(args.out, w, h, raw, args.chrome)
    print(f"OK {w}x{h} -> {args.out}   [{title}] via {how}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
