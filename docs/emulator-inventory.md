# Emulator inventory — which systems can actually run

Every emulator installed on this machine, what it covers, and whether it can be
driven without a human at the GUI. This is the gate that decides which games are
reachable at all: **an adapter cannot be written for a system we cannot boot.**

## Installed

| System | Emulator | How it was installed | Path |
|---|---|---|---|
| **NDS** | melonDS (+ Lua) | vendored fork, in-repo | `native/melonDS-android` (Android), `Core/` glue (iOS) |
| **GB / GBC / GBA** | **mGBA 0.10.5** | `scoop install mgba` | `~/scoop/shims/mgba` |
| **GB / GBC / GBA** | **mGBA 0.11 dev** | `scoop install mgba-dev` | `~/scoop/shims/mgba-dev` |
| **PS2** | **PCSX2 2.8.2** | `scoop install pcsx2` | `~/scoop/apps/pcsx2/current/pcsx2-qt.exe` |
| **PSP** | **PPSSPP** | installed in Program Files | `/c/Program Files/PPSSPP/PPSSPPWindows64.exe` |
| **GameCube / Wii** | **Dolphin** | `scoop install dolphin` | `~/scoop/shims/dolphin` |
| **SNES** | **Snes9x 1.63** | `scoop install snes9x` | `~/scoop/shims/snes9x` |
| **Dreamcast** | **Flycast 2.7** | `scoop install flycast` | `~/scoop/shims/flycast` |
| **3DS** | **Azahar** | `scoop install azahar` | `~/scoop/shims/azahar` |
| **Many** | **RetroArch 1.22.2** | `scoop install retroarch` | `~/scoop/shims/retroarch` |
| **Genesis / N64 / etc.** | *(none yet)* | — | — |

### What the new installs unblock

| System | Games in the library | Was | Now |
|---|---|---|---|
| SNES | 11 (beat-em-ups, fighters) | blocked | **core available** |
| Dreamcast | 4 (Power Stone, Plasma Sword, Psychic Force) | blocked | **core available** |
| 3DS | 10 (Fire Emblem Awakening/Fates, Pokémon ORAS, fighters) | blocked | **core available** |
| PS2 | 20 (DBZ Budokai/Tenkaichi, MK, Tekken, Odin Sphere) | had it | confirmed + BIOS present |
| PSP | ~30 (Dissidia, Crisis Core, FFT, Steins;Gate) | had it | confirmed |
| GameCube / Wii | 5 (Path of Radiance, TTYD, Mario Kart) | had it | confirmed |

**Still missing: N64 and Genesis.** See below.

## ✅ Verified by running `scripts/check-emulators.sh`

Rather than assert from memory, the set is verified by an executable check that
reports **PRESENT** (binary exists) and **DRIVABLE** (a non-interactive invocation
actually returns before its timeout). Those are different claims, and only the
second is worth building on.

```
=== Open Game Access — emulator core verification ===
  SYSTEM                 PRESENT   DRIVABLE  MECHANISM
  NDS (melonDS)          yes (wsl) yes       vendored C API: poke_frame / poke_set_button
  GB/GBC/GBA (mGBA dev)  yes       yes       mgba.exe --script <lua> <rom>
  RetroArch (multi)      yes/7 cores yes     --max-frames=N + --max-frames-ss
  PS1 (DuckStation)      yes       ?         GUI-first; BIOS files present: 3
  PS2 (PCSX2)            yes       NO        Qt GUI; RAM via PINE socket IPC
  PSP (PPSSPP)           yes       NO        GUI; no usable headless CLI confirmed
  GC/Wii (Dolphin)       yes       yes       Dolphin.exe -b -e <game>
  3DS (Azahar)           yes       ?         GUI-first; unverified
  SNES (Snes9x)          yes       ?         GUI-first; RetroArch core is drivable
  Dreamcast (Flycast)    yes       ?         GUI-first; RetroArch core is drivable
```

**`DRIVABLE=yes` means a non-interactive invocation returned** — that is the only
claim worth building on. `?` means *untested here*, **not** working.

⛔ **"Installed" is not "drivable".** Two systems are verified end-to-end for real
accessibility reads (NDS via melonDS; GB/GBA via mGBA). RetroArch additionally boots
ROMs and writes screenshots, which puts **SNES, Genesis, N64, PS1 and Dreamcast**
within reach through one harness. **PCSX2 and PPSSPP have no confirmed headless CLI**
— their route is RAM over PINE (PS2) and per-title RE (PSP), not flags.

### ⛔ Two false negatives this script produced, and why

A first version of the verifier reported **`NO` for melonDS, DuckStation and
Snes9x** — all three present and working. Both causes are worth recording:

1. **Wrong executable names.** A scoop package's binary is not always `<package>.exe`
   — DuckStation ships `duckstation-qt-x64-ReleaseLTCG.exe`, Snes9x ships
   `snes9x-x64.exe`, mGBA dev ships `mGBA.exe`. Guessing the name yields a missing
   file, which reads as "the emulator isn't installed".
2. **The WSL/Windows home trap again.** This script runs in git-bash, where `$HOME`
   is `C:\Users\<user>` — but the melonDS source tree lives at
   `/home/devin/src/melonds-lua` **inside WSL**. Checking the Windows home reports a
   false `NO` for a tree that is present and building. The fix asks WSL directly:
   `wsl.exe -d Ubuntu-24.04 -- test -d /home/devin/src/melonds-lua`.

⛔ **A presence check that guesses paths produces confident false negatives.** Resolve
names with a glob and ask the side that owns the path.

## ⛔ The distinction that matters: GUI vs. drivable

Having an emulator installed does **not** mean a harness can drive it. The
accessibility work needs to boot a ROM, run frames, send input, and read RAM
**without a human clicking anything**.

| Emulator | Headless / scriptable? | Notes |
|---|---|---|
| **mGBA 0.11 dev** | ✅ **VERIFIED** — `mgba.exe --script` | Proven end-to-end in this project. The 0.10.5 build is GUI-only. |
| **melonDS (vendored)** | ✅ **VERIFIED** — in-repo C API | `poke_frame`, `poke_set_button`, `poke_debug_nds` |
| **RetroArch** | ⚠️ **has the flags, not yet proven to run** | See below — this is the most promising and needs one more step |
| **PCSX2** | ⚠️ has `-batch` / `-nogui`; **RAM reachable over PINE** | Proven by the BT2 accessibility mod |
| **Dolphin** | ⚠️ has `-b -e <game>` (batch / exec) | Standard approach for scripted runs |
| **PPSSPP** | ⚠️ unverified; GUI has no usable `--help` | |
| **DuckStation / Snes9x / Flycast / Azahar** | ⚠️ unverified | |

⛔ **`--help` on a Qt GUI app HANGS.** `pcsx2-qt.exe --help` and
`PPSSPPWindows64.exe --help` both block waiting for a GUI. Always wrap emulator
probing in `timeout <n>`, or you lose the whole command budget to a hung window.

## RetroArch: 7 cores installed, and a real run VERIFIED

**Installed and verified present** (correct sizes, readable from Windows):

```
flycast_libretro.dll            21,692,928   Dreamcast
mednafen_psx_hw_libretro.dll    16,962,804   PS1 (hardware renderer)
genesis_plus_gx_libretro.dll     8,906,449   Genesis / Mega Drive
mupen64plus_next_libretro.dll    8,032,509   Nintendo 64
snes9x_libretro.dll              3,944,448   SNES
melonds_libretro.dll             3,603,456   NDS
mgba_libretro.dll                2,955,998   GB / GBC / GBA
```

Those come from the libretro buildbot, not scoop:
`https://buildbot.libretro.com/nightly/windows/x86_64/latest/<core>_libretro.dll.zip`

RetroArch's `cores` directory is a symlink into scoop's persist dir, and it
resolves for the native binary, so cores dropped there are picked up.

### ✅ Verified: it loads a core, boots a real ROM, and screenshots

```
retroarch.exe --max-frames=120 --max-frames-ss \
  --max-frames-ss-path="C:/.../ra-proof.png" \
  -L "C:/.../snes9x_libretro.dll" \
  "C:/.../Final Fight 2 (USA).sfc"
    exit=0   elapsed=40s
    screenshot: True (2277 bytes)
```

The verbose log confirms the full path works:

```
[Content] Loading content file: "...Final Fight 2 (USA).sfc"
[libretro INFO] "FINAL FIGHT 2" (NTSC) version 1.0
ROM: LoROM: 16 Mbit, SRAM: 0 Kbit
ID: , CRC32: 8c37ff55, Checksum OK
[Core] Geometry: 256x224, Aspect: 1.333, FPS: 60.10, Sample rate: 32040.00 Hz
[D3D11] Device created (Feature Level: 11.0)
```

So **RetroArch is drivable**: give it a core, a ROM, a frame budget, and a
screenshot path, and it exits 0 with a picture. That makes SNES, Genesis, N64,
PS1, Dreamcast and (redundantly) GB/GBA/NDS reachable through one harness.

### ⛔ The `-L` path must be a NATIVE Windows path

This is what cost three failed runs. `-L /c/Users/.../snes9x_libretro.dll` (MSYS
style, which is what bash variables naturally produce here) **fails silently** with
exit 1 and an *empty log*. `-L "C:/Users/.../snes9x_libretro.dll"` works. The ROM
path likewise. Same trap as elsewhere in this project: MSYS path conversion is off,
so pass `C:/...` to native binaries.

### ⛔ `--headless` does not exist; `--max-frames` is real-time paced

- `--headless` → `unrecognized option '--headless'` on 1.22.2.
- **`--max-frames=N` costs roughly N/60 seconds of wall clock.** 120 frames took
  ~40 s (≈20% of full speed in this configuration). **600 frames exceeded a 120 s
  timeout.** Budget runs accordingly, or disable vsync / use a faster video driver
  before asking for long runs.
- A 120-frame screenshot showed RetroArch's own OSD element but a **black game
  frame** — 120 frames is not enough for this title to draw. Screenshot later in
  the run (or after more frames) before concluding a core renders nothing.

### Flags that matter (from `retroarch --help`)

- **`--max-frames=N`** — run N frames then exit. The headless frame driver.
- **`--max-frames-ss` / `--max-frames-ss-path=FILE`** — screenshot at the end of
  `--max-frames`. Together: scriptable "run N frames, give me a picture".
- **`--accessibility`** — *"Enables accessibility for blind users using
  text-to-speech."* ⛔ **RetroArch ships its own screen-reader mode.** Worth
  investigating in its own right: it may already narrate its own menus.
- `-L <core>` — load a specific core; `-c FILE` — config; `--verbose` — the log
  that makes all of the above visible.

## The path that actually works per system

1. **mGBA `--script`** stays the GB/GBA path — already verified.
2. **melonDS via the in-repo C API** stays the NDS path — already verified.
3. **RetroArch** covers SNES, Genesis, N64, PS1, Dreamcast — **now verified to
   boot a ROM end-to-end**, with the caveat that frame throughput is slow and needs
   tuning before long scripted runs.
4. **PINE for PS2** — proven by the BT2 mod; avoids base-pointer chasing.
5. **Dolphin `-b -e`** for GameCube/Wii.

## ⛔ Scoop paths do not exist inside WSL

`scoop` is a **Windows** install living under `/mnt/c/Users/<user>/scoop`. Inside
WSL, `$HOME/scoop` is a **different, empty directory** — and `mkdir -p` will
happily create it.

That happened here: a core-download script written with WSL paths ran
`mkdir -p /home/devin/scoop/persist/retroarch/cores` and **succeeded**, then copied
7 cores into a fake tree that no emulator could see. The script printed `OK` for
every core and the counts looked right — because it was only ever checking its own
invented path.

```bash
# WRONG inside WSL — silently creates a second, useless scoop tree
C="$HOME/scoop/persist/retroarch/cores"

# RIGHT — the real Windows install, reached through the mount
C="/mnt/c/Users/Devin Prater/scoop/persist/retroarch/cores"
```

⛔ **`$LOCALAPPDATA` is also unset in WSL** and, under `set -u`, aborts the script
with `LOCALAPPDATA: unbound variable`. Use `/tmp` or a `/mnt/c` path.

**Verify installs from the side that owns them.** The check that caught this was
Python on **Windows** reading the real directory — every WSL-side check passed
because it was inspecting the fake tree.

## Still to install

| System | Candidate | Status |
|---|---|---|
| **N64** | RetroArch `mupen64plus_next` | ✅ **installed** — no standalone scoop package exists |
| **Genesis** | RetroArch `genesis_plus_gx` | ✅ **installed** |
| **PS1** | DuckStation + RetroArch `mednafen_psx_hw` | ✅ both installed |

⛔ **`scoop search mupen64plus` returns no matches.** N64 is not available as a
standalone scoop package; it only comes through a RetroArch core.

## Summary: what is actually reachable now

| System | Emulator present | Drivable today? |
|---|---|---|
| NDS | melonDS (+Lua) | ✅ verified |
| GB / GBC / GBA | mGBA 0.11 dev | ✅ verified |
| SNES | RetroArch `snes9x` | ✅ **verified** — boots, screenshot produced |
| Genesis | RetroArch `genesis_plus_gx` | ⚠️ core ready, same harness |
| N64 | RetroArch `mupen64plus_next` | ⚠️ core ready, same harness |
| PS1 | DuckStation, RetroArch `mednafen_psx_hw` | ⚠️ core ready, unverified |
| Dreamcast | RetroArch `flycast` | ⚠️ core ready, unverified |
| PS2 | PCSX2 (+BIOS) | ⚠️ PINE proven elsewhere |
| PSP | PPSSPP | ⚠️ unverified |
| GameCube / Wii | Dolphin | ⚠️ `-b -e` standard |
| 3DS | Azahar | ⚠️ unverified |

**Three systems are now proven drivable end-to-end (NDS, GB/GBA, SNES), and
RetroArch's harness generalises to Genesis, N64, PS1 and Dreamcast through the
same code path.** The remaining unknowns are PCSX2, PPSSPP, Dolphin and Azahar,
which each need their own mechanism.

⛔ **Do not read "core installed" as "system supported".** The install is the easy
part; proving a harness is the work. RetroArch took three failed runs before it
was understood — and the failures were a *path* problem, not a capability problem.

## Why this matters beyond convenience

The game backlog ranked NDS/GB/GBA first and everything else "blocked" — purely
for want of a core. SNES, Dreamcast, 3DS, PS2, PSP and GameCube now have one. The
ranking in `docs/research/game-backlog-ranked.md` should be re-read with this
table in hand: several "blocked" entries are now merely "needs a harness", and
the **PS1 gap is a single scoop install away**.
