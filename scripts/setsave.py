#!/usr/bin/env python3
# setsave.py — what does CartCommon::SetSaveMemory do when given NO save data?
# Android always passes a real SRAM buffer and requires a .sav file to exist;
# this path passes std::nullopt (savelen 0). If melonDS leaves the save chip at
# zero length, a game that inspects its save at boot would spin.
import re, sys

p = sys.argv[1] if len(sys.argv) > 1 else '/home/devin/src/melonds-lua/src/NDSCart/CartCommon.cpp'
src = open(p, encoding='utf-8', errors='replace').read()

def grab(name):
    i = src.find(name)
    if i < 0:
        print(f"!! {name} not found"); return
    # find the opening brace and match to the closing one
    j = src.find('{', i)
    if j < 0: return
    depth = 0
    for k in range(j, len(src)):
        if src[k] == '{': depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0:
                print(src[i:k+1]); print('-'*60); return

grab('void CartCommon::SetSaveMemory')
grab('CartCommon::CartCommon(')
print("=== SaveMemSize related members ===")
for m in re.finditer(r'^\s*(u32|bool|u8\*|std::unique_ptr<u8\[\]>)\s+(SaveMem\w*|SaveMemSize)\b.*$', src, re.M):
    print(' ', m.group(0).strip())
print()
print("=== CartRetail ctor + save chip selection ===")
grab('CartRetail::CartRetail')
