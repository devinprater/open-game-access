#!/usr/bin/env python3
"""fe-read: OCR in, exact text out. F1-scored sliding windows, no expansion."""
import sys, re

def words(s):
    return set(re.findall(r"[A-Za-z']+", s.lower()))

db = []
for ln in open('/home/devin/fe/text-db.txt'):
    ln = ln.rstrip('\n')
    if '  ' in ln:
        a, s = ln.split('  ', 1)
        db.append(s)

wins = []
for i in range(len(db)):
    for k in (1, 2, 3):
        if i + k <= len(db):
            wins.append(' '.join(db[i:i + k]))

ocr = sys.stdin.read()
ows = words(ocr)
best, bestf = None, 0.0
for wtxt in wins:
    sws = words(wtxt)
    if not sws or not ows:
        continue
    inter = len(ows & sws)
    p = inter / len(ows)
    r = inter / len(sws)
    f1 = 2 * p * r / (p + r) if (p + r) else 0.0
    if f1 > bestf:
        bestf, best = f1, wtxt
print('SCORE: %.2f' % bestf)
print('READ: ' + (best or '(none)'))
