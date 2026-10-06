import struct, zlib, sys

P = "/mnt/c/Users/Devin Prater/Documents/PPSSPP/PSP/SAVEDATA/ULUS102340000/DATA.BIN"
d = open(P, "rb").read()
print("DATA.BIN %d bytes" % len(d))
print("first 0x60:", d[:0x60].hex(" "))
print()

# PSP saves are usually encrypted/signed by the game, but many games leave a plain block.
# Report entropy in 256-byte windows so a plain zone is visible.
import math
def ent(b):
    if not b: return 0.0
    c = [0]*256
    for x in b: c[x]+=1
    h = 0.0
    for v in c:
        if v:
            p = v/len(b); h -= p*math.log2(p)
    return h

print("entropy per 512-byte window (first 24 windows):")
for i in range(0, min(len(d), 512*24), 512):
    w = d[i:i+512]
    print("  +0x%05X  %.2f  %s" % (i, ent(w), w[:16].hex(" ")))
print()

# look for the roster character ids or small ordinal runs
# Another Road characters are 0..23; a 24-entry flag/id array would be a run of small bytes
print("runs of >=16 small bytes (<=0x20) in the first 0x2000:")
for i in range(0, min(len(d), 0x2000) - 16):
    if all(d[j] <= 0x20 for j in range(i, i+16)):
        print("  0x%04X: %s" % (i, d[i:i+24].hex(" ")))
print()

# zlib streams?
print("zlib/gzip candidates:")
for i in range(0, len(d)-2):
    if d[i] == 0x78 and d[i+1] in (0x01, 0x9c, 0xda):
        try:
            zlib.decompress(d[i:])
            print("  decompressible zlib at 0x%04X" % i)
        except Exception:
            pass
print()

# printable ASCII runs
print("ASCII strings >=8:")
i = 0
n = 0
while i < len(d) and n < 30:
    if 0x20 <= d[i] < 0x7f:
        j = i
        while j < len(d) and 0x20 <= d[j] < 0x7f: j += 1
        if j - i >= 8:
            print("  0x%04X  %r" % (i, d[i:j].decode("latin1")))
            n += 1
        i = j
    else:
        i += 1
