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

## RetroArch: 7 cores installed, CLI verified, run NOT yet proven

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

RetroArch's own `cores` directory is a symlink to scoop's persist dir, so cores
dropped there are picked up.

### The CLI flags that matter (from `retroarch --help`)

- **`--max-frames=N`** — run N frames then exit. This is the headless frame driver.
- **`--max-frames-ss` / `--max-frames-ss-path=FILE`** — screenshot at the end of
  `--max-frames`. Combined, these are a scriptable "run N frames and give me a
  picture" — exactly what a probe needs.
- **`--accessibility`** — *"Enables accessibility for blind users using
  text-to-speech."* ⛔ **RetroArch ships its own screen-reader mode.** Worth
  investigating in its own right: it may already narrate its menus, which is work
  this project would otherwise redo per emulator.
- `-L <core>` — load a specific core; `-c FILE` — config.

⛔ **`--headless` DOES NOT EXIST on this build** (1.22.2): it fails with
`unrecognized option '--headless'`. Do not plan around it.

⚠️ **A `--max-frames` run still exited 1 with an EMPTY log**, which is what a GUI
app does when it has no window to draw into. So the flags are present and correct
but a real run is **not yet proven**. Next step is either a virtual display or
checking whether the exit-1 is a config/save-dir problem rather than a window one
— an empty log means it failed before logging, not that it refused the core.

## The path that actually works per system

1. **mGBA `--script`** stays the GB/GBA path — already verified.
2. **melonDS via the in-repo C API** stays the NDS path — already verified.
3. **RetroArch, once the run is proven**, covers SNES, Genesis, N64, PS1 and
   Dreamcast through one integration. That is five systems for the price of one
   harness, which is why it is worth the extra step.
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
| SNES | Snes9x, RetroArch core | ⚠️ core ready, harness unproven |
| Genesis | RetroArch core | ⚠️ as above |
| N64 | RetroArch core | ⚠️ as above |
| PS1 | DuckStation, RetroArch core | ⚠️ unverified |
| PS2 | PCSX2 (+BIOS) | ⚠️ PINE proven elsewhere |
| PSP | PPSSPP | ⚠️ unverified |
| GameCube / Wii | Dolphin | ⚠️ `-b -e` standard |
| Dreamcast | Flycast, RetroArch core | ⚠️ unverified |
| 3DS | Azahar | ⚠️ unverified |

**Two systems are proven drivable. Nine have a core and a candidate mechanism
but no verified harness.** The distinction matters: a core install is a
twenty-minute job, while proving a harness is the real work — and the backlog
ranking should be read with that in mind.

## Why this matters beyond convenience

The game backlog ranked NDS/GB/GBA first and everything else "blocked" — purely
for want of a core. SNES, Dreamcast, 3DS, PS2, PSP and GameCube now have one. The
ranking in `docs/research/game-backlog-ranked.md` should be re-read with this
table in hand: several "blocked" entries are now merely "needs a harness", and
the **PS1 gap is a single scoop install away**.
