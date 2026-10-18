# SNES Console for Open Game Access — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Super Nintendo console in OGA that loads a ROM, runs it, shows it, takes input, saves battery RAM and state, and lets an adapter read its 128 KiB of work RAM.

**Architecture:** One new core file (`Core/snes_core.cpp`) wrapping MesenCE's `SnesConsole` exactly the way `Core/mesen_core.cpp` wraps its NES console: Mesen `Run()` on its own thread, the host stepping via frame-count waits, reads through `MemoryType::SnesWorkRam`. Backend wiring follows the existing `OgaCoreOps` seam, and the ROM is identified by CRC32, never by filename.

**Tech Stack:** C++17, MesenCE (`nesdev-org/MesenCE`), the repo's existing host harness scripts, Swift 6 front end for the app build.

**Spec:** `docs/design/alttp-adapter.md`

## Global Constraints

- Never commit a ROM, a ROM-derived asset, or a byte of game data. `scripts/check-no-roms.sh` guards this.
- Every memory address used by code is measured before it is used. A name from a decompilation is a hypothesis until a live read confirms it.
- Refusal over guessing: a console or adapter that cannot read returns `false` or `""`, never a plausible value.
- The C ABI is append-only. Do not reorder `Command`, `OgaSystemId`, or `POKE_BTN_*`.
- Mesen source is `nesdev-org/MesenCE` at `$HOME/src/mesen`. Never `SourMesen/Mesen2`.
- No audio in this plan. The SNES core returns 0 from audio, like the NES core.
- Commits go on `main`, no PRs. One commit per task.
- `scripts/adapter-tests.sh` and `scripts/systems-test.sh` stay green at every commit.
- Test ROM: `${ALTP_ROM:-/mnt/c/Users/Devin Prater/Dropbox/games/snes/Legend of Zelda, The - A Link to the Past (USA).sfc}` (CRC32 `777AAC2F`).

## Review Focus

- **A copier-headered `.smc`.** Many dumps carry a 512-byte header. The core must boot it and report the same CRC as the headerless file, or refuse loudly. Quietly booting at the wrong offset would read garbage RAM and speak nonsense.
- **A renamed or hacked ROM.** It must load, report `crc:…`, and attach no reader. No speech, no reader controls.
- **A `.gb` or `.gbc` file.** Mesen's SNES class lists `.gb` among its extensions. The resolver must still send Game Boy files to the mGBA backend, never to this core.
- **Loading a second game after a first.** Teardown must leave no stale adapter, no leaked console, and the new ROM's id must be what is reported.
- **A console replaced or reset mid-run.** Mesen swaps the console object on reset. The frame wait must not hang or false-fail, and reads must follow the live console.

---

### Task 1: Core skeleton, ROM load, identity

**Files:**
- Create: `Core/snes_core.h`
- Create: `Core/snes_core.cpp`
- Create: `Core/snes_probe.cpp`
- Create: `scripts/snes-alttp-proof.sh`

**Interfaces:**
- Produces: `SnesCore *snes_create(void)`, `void snes_destroy(SnesCore *)`
- Produces: `bool snes_load_rom(SnesCore *, const char *rom_path, const char *save_path, char code_out[16])`
- Produces: `uint32_t snes_rom_crc32(SnesCore *)`, `const char *snes_last_error(SnesCore *)`

- [ ] **Step 1: Write the failing probe**

`Core/snes_probe.cpp` takes `--identity <rom> <savedir>`, creates a core, loads the ROM, and prints:

```
crc=777aac2f
code=crc:777aac2f
```

It exits 1 when the CRC differs from `0x777AAC2F` or the code string is not `crc:777aac2f`.

- [ ] **Step 2: Run it and confirm it cannot build**

Run: `bash scripts/snes-alttp-proof.sh --identity`
Expected: compile error naming `snes_core.h` — the header does not exist yet.

- [ ] **Step 3: Implement the header and the load path**

`snes_core.h` mirrors `mesen_core.h`'s shape. `snes_load_rom` does exactly what `nes_load_rom` does, in this order: `FolderUtilities::SetHomeFolder` from `save_path`; `Emulator` construction and `Initialize()`; `VirtualFile` open; `LoadRom` inside `try/catch` with the exception text written to the core's error; `GetConsole()`; CRC from `emu->GetCrc32()`; write `snprintf(code_out, 16, "crc:%08x", crc)`.

- [ ] **Step 4: Run the probe**

Run: `bash scripts/snes-alttp-proof.sh --identity`
Expected: both lines above, exit 0.

- [ ] **Step 5: Prove the copier-header case**

Prepend 512 zero bytes to a copy of the ROM at `$HOME/z3work/alttp-headered.smc` and run the same probe on it.
Expected: the same CRC and exit 0. If Mesen does not strip the header, the probe must refuse with a message naming the header; record the outcome in the script's comment.

- [ ] **Step 6: Commit**

```bash
git add Core/snes_core.h Core/snes_core.cpp Core/snes_probe.cpp scripts/snes-alttp-proof.sh
git commit -m "snes: core skeleton, ROM load and CRC identity"
```

---

### Task 2: Frames, video, input, stop

**Files:**
- Modify: `Core/snes_core.h`
- Modify: `Core/snes_core.cpp`
- Modify: `Core/snes_probe.cpp`

**Interfaces:**
- Produces: `bool snes_start(SnesCore *)`, `void snes_stop(SnesCore *)`, `bool snes_running(SnesCore *)`
- Produces: `bool snes_frame(SnesCore *)`, `const char *snes_frame_result(SnesCore *)`, `void snes_reset(SnesCore *)`
- Produces: `bool snes_framebuffer(SnesCore *, int *w, int *h)`, `const uint8_t *snes_framebuffer_ptr(SnesCore *)`
- Produces: `void snes_set_button(SnesCore *, int index, bool down)`, `void snes_set_uncapped(SnesCore *, bool)`
- Produces: `unsigned long long snes_frames_completed(SnesCore *)`, `unsigned snes_console_frame_count(SnesCore *)`, `double snes_fps(SnesCore *)`

- [ ] **Step 1: Add the run mode and make it fail**

`--run <rom> <savedir> <frames>` prints `frames=<n> console=<n> fb=<w>x<h> hash=<8 hex>`, where the hash is FNV-1a over the framebuffer copy. It fails when `frames` reaches the requested count but `console` did not advance, when `w` or `h` is zero, or when the hash is the all-black constant.

- [ ] **Step 2: Run it**

Run: `bash scripts/snes-alttp-proof.sh --run 600`
Expected: FAIL — `snes_start` is undefined.

- [ ] **Step 3: Implement start, frame, video, input, stop**

Copy the NES core's threading and frame-wait logic, including `LiveConsole()`, the replaced-console and counter-reset branches, the 5-second bounded wait, and the framebuffer copy. Register a `SnesInputProvider` before the loop starts. Its `SetInput` claims only SNES controller types and writes the 12 pad bits through `device->SetBitValue`. `snes_stop` unregisters the provider, calls `Emulator::Stop(false, true, true)`, joins the thread, and calls `console->SaveBattery()` before releasing the console. `snes_reset` calls `Emulator::Reset()`, and `snes_set_uncapped` sets `EmulationFlags::MaximumSpeed` like the NES core does.

- [ ] **Step 4: Run it**

Run: `bash scripts/snes-alttp-proof.sh --run 600`
Expected: 600 frames, console count advanced by at least 600, nonzero dimensions, a non-black hash.

- [ ] **Step 5: Prove input reaches the game**

`--input <rom> <savedir>` runs 600 frames with no input and records the hash, then reruns from a fresh core holding Start from frame 120 to 150. The title screen changes, so the two hashes must differ.

Run: `bash scripts/snes-alttp-proof.sh --input`
Expected: `input-changes-video=YES`.

- [ ] **Step 6: Prove a reset does not hang the frame wait**

`--reset` runs 120 frames, calls the emulator's reset through `snes_reset`, then runs 120 more and requires both calls to return true inside the script's timeout.

- [ ] **Step 7: Commit**

```bash
git add Core/snes_core.h Core/snes_core.cpp Core/snes_probe.cpp scripts/snes-alttp-proof.sh
git commit -m "snes: frame loop, video, input and clean stop"
```

---

### Task 3: Work-RAM reads and diagnostics

**Files:**
- Modify: `Core/snes_core.h`
- Modify: `Core/snes_core.cpp`
- Modify: `Core/snes_probe.cpp`

**Interfaces:**
- Produces: `bool snes_read(SnesCore *, uint32_t addr, int width, uint32_t *out)`
- Produces: `uint32_t snes_ram_base(void)` = `0x7E0000`, `uint32_t snes_ram_size(void)` = `0x20000`
- Produces: `int snes_cpu_pc(SnesCore *)` via `SnesCpu::GetState().PC`

- [ ] **Step 1: Write the read assertions**

`--reads <rom> <savedir>` asserts, printing one line each:

```
in-range-byte        ok
in-range-word        ok
frame-counter-moves  ok   (value at 0x7E001A differs after 60 frames)
far-bank-word        ok   (0x7F0000 reads)
mmio-refused         ok   (0x002100 returns false)
wrap-refused         ok   (0x7FFFFF, width 2 returns false)
bad-width-refused    ok   (width 0 and 3 return false)
```

- [ ] **Step 2: Run it**

Run: `bash scripts/snes-alttp-proof.sh --reads`
Expected: FAIL at the first assertion, because `snes_read` is undefined.

- [ ] **Step 3: Implement the reads**

Take the buffer from `emu->GetMemory(MemoryType::SnesWorkRam)` and bounds-check against its `Size`. Translate `0x7E0000..0x7FFFFF` to an offset. Refuse anything else. Do not read through the CPU bus: an address like `$2100` has side effects when read.

- [ ] **Step 4: Run it**

Run: `bash scripts/snes-alttp-proof.sh --reads`
Expected: all seven lines `ok`.

- [ ] **Step 5: Commit**

```bash
git add Core/snes_core.h Core/snes_core.cpp Core/snes_probe.cpp scripts/snes-alttp-proof.sh
git commit -m "snes: work-RAM reads with bounds and no MMIO"
```

---

### Task 4: Backend wiring — resolver, ops table, load path

**Files:**
- Modify: `Core/oga_core.h` (the `OgaResolvedBackend` struct)
- Modify: `Core/oga_core.cpp` (ops table after the NES table, resolver at the bottom)
- Modify: `Core/pokecore.cpp` (`TeardownBackends`, a new `LoadSnesRom`, the dispatch in `poke_load_rom`)
- Modify: `Core/snes_core.h`
- Modify: `scripts/core-sources.sh` (`OGA_GLUE`)

**Interfaces:**
- Consumes: Task 1–3's `snes_*` functions.
- Produces: `OgaCore oga_snes_core(SnesCore *)`, `bool snes_hint` in `OgaResolvedBackend`.

- [ ] **Step 1: Add the resolver assertions**

`--resolve` prints one line per path:

```
x.sfc  -> snes
x.smc  -> snes
x.fig  -> snes
x.swc  -> snes
x.gb   -> gba
x.nes  -> nes
x.nds  -> nds
x.xyz  -> (none)
```

- [ ] **Step 2: Run it**

Run: `bash scripts/snes-alttp-proof.sh --resolve`
Expected: FAIL, because `.sfc` returns `(none)`.

- [ ] **Step 3: Implement**

Add `snes_hint` as a trailing field of `OgaResolvedBackend` so the three existing initializers keep compiling. Add the four SNES extensions to the resolver. Add the `kSnesOps` table: start, stop, frame, read, framebuffer, set_button, no analog, no touch, no hotkey, no script dir, no reader set, no audio, save/load state, frames, last error. Add `snes_core.cpp` to `OGA_GLUE`. In `pokecore.cpp`, add the `SnesCore *snes` member, destroy it in `TeardownBackends`, add `LoadSnesRom` (create, load, set backend, copy `gameCode`, select `find_by_game_code(core->gameCode)`), and dispatch on `snes_hint`.

- [ ] **Step 4: Run it**

Run: `bash scripts/snes-alttp-proof.sh --resolve`
Expected: every line matches the table, including `x.gb -> gba`.

- [ ] **Step 5: Build the host object set**

Run: `bash scripts/build-host.sh`
Expected: no `.failed` file under `Vendor/hostobj`, and `snes_core.o` present.

- [ ] **Step 6: Commit**

```bash
git add Core/oga_core.h Core/oga_core.cpp Core/pokecore.cpp Core/snes_core.h scripts/core-sources.sh
git commit -m "snes: backend ops table, resolver case and load path"
```

---

### Task 5: Registry READY and the app-path proof

**Files:**
- Modify: `Core/systems.cpp` (the SNES row)
- Modify: `Core/systems_test.cpp` (the `planned[]` array)
- Create: `scripts/snes-live-proof.cpp`
- Modify: `scripts/snes-alttp-proof.sh`

**Interfaces:**
- Consumes: `poke_load_rom`, `poke_start`, `poke_frame`, `poke_game_code`, `poke_adapter_id`, `poke_last_error`.

- [ ] **Step 1: Make the registry test demand READY**

Remove `OGA_SYS_SNES` from the `planned[]` array in `systems_test.cpp`, and add a positive assertion beside the NES's: the SNES row is runnable and reports the Mesen backend id.

- [ ] **Step 2: Run it and watch it fail**

Run: `bash scripts/systems-test.sh`
Expected: FAIL — "one claimed to be runnable" style failure, because the row still says `OGA_BACKEND_PLANNED`.

- [ ] **Step 3: Flip the row**

In `Core/systems.cpp`, change the SNES row's backend to `OGA_BACKEND_READY` and leave every hardware fact as it is: 1 screen, shoulders, no touch, no analog, d-pad, `kDiamond`, 4 face buttons.

- [ ] **Step 4: Run it**

Run: `bash scripts/systems-test.sh`
Expected: PASS.

- [ ] **Step 5: Write the app-path proof**

`scripts/snes-live-proof.cpp` mirrors `scripts/nes-live-proof-main.cpp`: load through `poke_load_rom`, assert the adapter-less id `snes`, assert `poke_game_code` is `crc:777aac2f`, run 300 frames, assert `poke_adapter_ready` is 0 and nothing was spoken.

- [ ] **Step 6: Prove the renamed copy, the second load and an unknown dump**

The script copies the ROM to `$HOME/z3work/alttp-renamed.sfc` and runs the proof again: same id, same refusal. Then it loads the NES fixture first and the SNES ROM second in one process, and requires the reported backend and code to change with no crash. Finally it loads a different SNES game (`Chrono Trigger (USA).sfc` in the same folder) and requires: the backend is `snes`, the code is that file's own `crc:…`, the adapter is none, and nothing is spoken.

- [ ] **Step 7: Commit**

```bash
git add Core/systems.cpp Core/systems_test.cpp scripts/snes-live-proof.cpp scripts/snes-alttp-proof.sh
git commit -m "snes: registry READY, app-path proof, renamed and reload cases"
```

---

### Task 6: Savestates

**Files:**
- Modify: `Core/snes_core.cpp`

**Interfaces:**
- Produces: `bool snes_save_state(SnesCore *, const char *path)`, `bool snes_load_state(SnesCore *, const char *path)`

- [ ] **Step 1: Add the state assertions**

`--states <rom> <savedir>` runs 120 frames, reads `0x7E001A`, saves to `<savedir>/alttp.state`, runs 60 frames, reads again (must differ), loads the state, reads again and requires exact equality with the saved value.

- [ ] **Step 2: Run it**

Run: `bash scripts/snes-alttp-proof.sh --states`
Expected: FAIL — save states are not implemented.

- [ ] **Step 3: Implement**

Call `emu->GetSaveStateManager()->SaveState(path)` and `LoadState(path)`. Mesen's manager takes its own locks; do not call these from the emulation thread. Record in a comment whether the emulator had to be paused and how that was confirmed.

- [ ] **Step 4: Run it**

Run: `bash scripts/snes-alttp-proof.sh --states`
Expected: `state-restores=YES`.

- [ ] **Step 5: Commit**

```bash
git add Core/snes_core.cpp
git commit -m "snes: savestates through Mesen's state manager"
```

---

### Task 7: Swift picker and app build

**Files:**
- Modify: `Sources/OpenGameAccess/InputBridge.swift:131` and `:186`

- [ ] **Step 1: Add the extensions**

Add `sfc`, `smc`, `fig`, `swc` to the per-system list at line 131 and to the picker filter at line 186. Leave `.gb`, `.gbc`, `.nes` untouched.

- [ ] **Step 2: Build the app**

Run: `bash scripts/xtool-build.sh`
Expected: build succeeds; the SNES extensions appear in the built binary's string table.

- [ ] **Step 3: Commit**

```bash
git add Sources/OpenGameAccess/InputBridge.swift
git commit -m "snes: offer .sfc/.smc/.fig/.swc in the ROM picker"
```

---

### Task 8: Android build and phone measurement (gated)

Do not start until Tasks 1–6 are committed. This task needs Devin's phone.

**⛔ MEASURED 2026-10-17 — THIS TASK CANNOT RUN AS WRITTEN.** The Android app links
only `melonds_bridge.cpp`: no Mesen, no `snes_core`, no `oga_core`, and no device was
attached. The iOS build is the phone path this plan delivers. The finding and the
evidence are in `docs/evidence/alttp-android-fps.txt`; an Android port is a separate plan.

**Files:**
- Create: `docs/evidence/alttp-android-fps.txt`

- [ ] **Step 1: Build and install**

Run: `bash scripts/android-build-local.sh`
Install the APK on Devin's phone with `adb install -r`.

- [ ] **Step 2: Measure**

Load the ROM, let the title screen run, and record in `docs/evidence/alttp-android-fps.txt`: device model, build hash, frames per second reported by the app's log, and whether the first ten seconds of gameplay is smooth or audible to Devin as stutter.

- [ ] **Step 3: Decide**

If the measured speed is below 60 fps with dropped frames on the phone, stop and write the finding in the evidence file before proposing any reader work. The reader is worthless if the game does not run.

- [ ] **Step 4: Commit the evidence**

```bash
git add docs/evidence/alttp-android-fps.txt
git commit -m "snes: Android speed measurement for A Link to the Past"
```

---

## Follow-on plan

The reader (identity, `WhereAmI`, `Status`, names, menus, dialogue) is a second plan, `docs/plans/alttp-reader.md`, written after this one lands because its menu and dialogue work depends on measurements this plan makes possible.
