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
| **mGBA 0.11 dev** | ✅ **YES** — `mgba.exe --script` | Verified end-to-end in this project. The 0.10.5 build is GUI-only. |
| **melonDS (vendored)** | ✅ **YES** — used through the in-repo C API | `poke_frame`, `poke_set_button`, `poke_debug_nds` |
| **PCSX2** | ⚠️ **has `-batch` / `-nogui`** but is a Qt app | Its RAM is reachable over **PINE** (socket IPC) — proven by the BT2 accessibility mod |
| **PPSSPP** | ⚠️ **has `--headless`** in newer builds; GUI has no usable `--help` here | Needs verification |
| **Dolphin** | ⚠️ **has `Dolphin.exe -b -e <game>`** (`-b` batch, `-e` exec) | Standard approach for scripted runs |
| **RetroArch** | ⚠️ **has `--headless`** and a UDP network command interface | The most portable option; cores must be present |
| **Snes9x** | ⚠️ GUI-first; RetroArch is the better harness path for SNES | |
| **Flycast** | ⚠️ has CLI options; unverified | |
| **Azahar** | ⚠️ Citra-lineage, unverified | |

⛔ **`--help` on a Qt GUI app HANGS.** `pcsx2-qt.exe --help` and
`PPSSPPWindows64.exe --help` both block waiting for a GUI. Always wrap emulator
probing in `timeout <n>`, or you lose the whole command budget to a hung window.

## Recommended path per system

1. **RetroArch for everything it has cores for.** It is the only emulator here
   designed to be driven programmatically: `--headless`, a network command
   interface, and per-system cores. One integration covers SNES, Genesis, N64 and
   more at once — far less work than one harness per emulator.
2. **PINE for PS2.** The BT2 mod proves this works, and it avoids base-pointer
   chasing entirely.
3. **Dolphin `-b -e`** for GameCube/Wii.
4. **mGBA `--script`** stays the GB/GBA path — already verified.

## Still to install

| System | Candidate | Status |
|---|---|---|
| **N64** | RetroArch core (`mupen64plus_next`) — `scoop search mupen64plus` found nothing standalone | install core via RetroArch |
| **Genesis** | RetroArch core (`genesis_plus_gx`) | install core via RetroArch |
| **PS1** | `duckstation` is available in scoop (`scoop search duckstation`) | **not installed yet — worth adding** |

⛔ **`scoop search mupen64plus` returns no matches.** N64 is not available as a
standalone scoop package; it has to come through a RetroArch core, which is
another argument for doing the RetroArch integration first.

## Why this matters beyond convenience

The game backlog ranked NDS/GB/GBA first and everything else "blocked" — purely
for want of a core. SNES, Dreamcast, 3DS, PS2, PSP and GameCube now have one. The
ranking in `docs/research/game-backlog-ranked.md` should be re-read with this
table in hand: several "blocked" entries are now merely "needs a harness", and
the **PS1 gap is a single scoop install away**.
