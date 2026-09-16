#!/usr/bin/env python3
"""fe-tutorial-steps.py — learn the Prologue's scripted steps by bisection.

⛔ WHY THIS INSTEAD OF ANOTHER PLAN ATTEMPT. FE11's Prologue is a chain of scripted
movement tutorials ("Marth can move anywhere within the blue area", "Move Marth farther
down the corridor", ...). A hand-authored keypress plan satisfies one step and then
stalls on the next, and there is no way to see WHY from memory alone — the tutorial
text lives on the top screen, and gMapStateManager is valid while a tutorial is up.

So: step through candidate frames, and at each one record
  * whether the map/cursor is readable (the tutorial is on a real map), and
  * which units exist and where.

That turns "the tutorial is stuck" into "the tutorial is stuck AT step N", which is
actionable. Screenshots are taken at the same frames so the text can be read directly.
"""
import os
import subprocess
import sys

ROOT = os.path.expanduser("~/open-game-access")
ROM = os.path.expanduser("~/roms/Fire Emblem - Shadow Dragon (USA).nds")
PLAN = sys.argv[1] if len(sys.argv) > 1 else "play"
FRAMES = [int(x) for x in (sys.argv[2].split(",") if len(sys.argv) > 2
                           else "2000,4000,6000,8000,10000,12000,14000".split(","))]

env = dict(os.environ)
env["PA_SHIM"] = f"{ROOT}/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
env.pop("SAVE", None)          # the Prologue is played from the title, no save

print(f"plan: {PLAN}   frames: {FRAMES}")
print()
for f in FRAMES:
    # Append a SHOT at the tail; fedump now sorts plan events by frame, so order is safe.
    planfile = f"/tmp/step-{f}.txt"
    with open(f"{ROOT}/fe/plans/{PLAN}.txt") as src:
        body = src.read()
    # Append a SHOT at the tail; fedump sorts plan events by frame, so order is safe.
    with open(planfile, "w") as dst:
        dst.write(body)
        dst.write(f"\nSHOT {f - 100} /home/devin/fe/out/step-{f}.ppm\n")

    r = subprocess.run([f"{ROOT}/Vendor/fedump", ROM, str(f), planfile],
                       capture_output=True, text=True, timeout=900, env=env)
    out = r.stdout
    cursor = next((l.strip() for l in out.splitlines() if l.startswith("Where am I")), "n/a")
    enemy = next((l.strip() for l in out.splitlines() if l.startswith("Next enemy")), "n/a")
    ter = next((l.strip() for l in out.splitlines() if "TERRAIN at cursor" in l), "")
    units = next((l.strip() for l in out.splitlines() if "live units" in l), "")
    print(f"f={f}")
    print(f"   {units}")
    print(f"   {cursor}")
    print(f"   {enemy}")
    if ter:
        print(f"   {ter}")
    print()
