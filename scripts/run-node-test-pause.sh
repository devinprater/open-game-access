#!/usr/bin/env bash
# Chain: open the PAUSE MENU (which is known to scroll with 'down'), verify it is static AND that
# 'down' actually moves the highlight, then run the node probe immediately (no drift gap).
set -u
cd "/c/Users/Devin Prater/open-game-access" || exit 1
S="$LOCALAPPDATA/Temp"
H="python.exe"
E="$LOCALAPPDATA/Temp"

echo "=== 1. open the pause menu (start) ==="
$H scripts/psp-press-sweep.py --buttons start --frames 14 --wait 3 >/dev/null 2>&1
sleep 3

$H scripts/psp-shot.py "$S/pm-1.png" >/dev/null 2>&1
sleep 2.5
$H scripts/psp-shot.py "$S/pm-2.png" >/dev/null 2>&1
echo "=== 2. static check (no press) ==="
$H -c "
from PIL import Image
import numpy as np, os
S=os.path.expandvars(r'%LOCALAPPDATA%\Temp')
a=np.asarray(Image.open(os.path.join(S,'pm-1.png')).convert('RGB')).astype(np.int16)
b=np.asarray(Image.open(os.path.join(S,'pm-2.png')).convert('RGB')).astype(np.int16)
d=int((np.abs(a-b).max(axis=2)>16).sum())
print('   no-press diff: %d px -> %s' % (d,'STATIC' if d<=2000 else 'ANIMATING'))
"
echo "=== 3. does 'down' move the highlight here? ==="
$H scripts/psp-press-sweep.py --buttons down --frames 12 --wait 1.5 >/dev/null 2>&1
sleep 1.5
$H scripts/psp-shot.py "$S/pm-3.png" >/dev/null 2>&1
$H -c "
from PIL import Image
import numpy as np, os
S=os.path.expandvars(r'%LOCALAPPDATA%\Temp')
a=np.asarray(Image.open(os.path.join(S,'pm-2.png')).convert('RGB')).astype(np.int16)
b=np.asarray(Image.open(os.path.join(S,'pm-3.png')).convert('RGB')).astype(np.int16)
d=int((np.abs(a-b).max(axis=2)>16).sum())
print('   down-press diff: %d px -> %s' % (d,'HIGHLIGHT MOVED' if d>0 else 'NO MOVEMENT'))
"
echo
echo "=== 4. node probe on this screen (its own guards re-verify) ==="
timeout 400 $H -u scripts/psp-probe-nodes.py --press down --rounds 12 2>&1 | tail -30
