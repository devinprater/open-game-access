#!/usr/bin/env bash
# fe-ocr2: crop to the text box, keep bright pixels only, upscale, OCR.
# Usage: fe-ocr2.sh <shot.ppm> <x0> <y0> <x1> <y1> [thresh]
python3 - "$@" <<'EOF'
import sys, os
a = sys.argv
src = a[1]; x0 = int(a[2]); y0 = int(a[3]); x1 = int(a[4]); y1 = int(a[5])
th = int(a[6]) if len(a) > 6 else 130
d = open(src, 'rb').read()
parts = d.split(b'\n', 3)
wh = parts[1].split()
w = int(wh[0]); h = int(wh[1])
px = parts[3]
S = 4
W = (x1 - x0) * S
H = (y1 - y0) * S
out = bytearray()
for y in range(y0, y1):
    line = bytearray()
    for x in range(x0, x1):
        o = (y * w + x) * 3
        r = px[o]; g = px[o + 1]; b = px[o + 2]
        lum = (r * 77 + g * 150 + b * 29) >> 8
        v = (0 if lum >= th else 255) if os.environ.get("FE_INVERT") else (255 if lum >= th else 0)
        line += bytes((v, v, v)) * S
    out += bytes(line) * S
open('/tmp/fe-ocr-big.ppm', 'wb').write(('P6\n' + str(W) + ' ' + str(H) + '\n255\n').encode() + bytes(out))
sys.stderr.write('crop done ' + str(W) + 'x' + str(H) + '\n')
EOF
tesseract /tmp/fe-ocr-big.ppm stdout --psm 6 2>/dev/null
