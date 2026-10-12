# psp-web-recomp — static recompilation of PSP games, and what it is worth to us

Studied 2026-10-12. **Nothing in this note comes from running the project.** Every
claim is read from its README, `docs/internals.md`, `scripts/`, the file tree and
the commit log over the GitHub API. Unverified items are marked.

Source: `github.com/snuri00/psp-web-recomp` (MIT, created 2026-10-07, 10 commits,
one author, ~245 stars). A profile for **PSPRecomp**, not a recompiler of its own.

---

## 1. What it is

PSP games running without an emulator. The game's decrypted MIPS executable is
translated ahead of time into C++ by `psp_recomp`, one translation unit per 16 KiB
of guest code, and linked against a hand-written reimplementation of the parts of
the PSP operating system a title actually calls, plus a WebGL2 renderer for the GE.

- `patches/` — five patches against PSPRecomp (codegen, VFPU registers, an ADDI
  decoder gap, the 16 KiB scratchpad at `0x00010000`, function discovery).
- `profile/host/` — the HLE kernel: `kernel.cpp` (threads, sync, memory, display and
  GE syscalls), `devices.cpp` (controller, audio mixing, UMD, dialogs), `io.cpp` and
  `webfs.cpp` (filesystem; the disc is streamed over HTTP Range requests in the
  browser), `pgd.cpp` (DRM), `mpeg.cpp` (movie bookkeeping, no decoding), `sas.cpp`
  and `atrac.cpp` (sound voices and ATRAC3+ music), `ge.cpp` (display lists, vertex
  processing, software rasterizer fallback), `ge_gl.cpp` (the GPU backend).
- `profile/web/shell.html` — the page: status bar, resolution menu, touch controls, AudioWorklet.
- `profile/third_party/at3_standalone/` — PPSSPP's ATRAC3+ decoder, vendored with its
  source commit. The same code our own PSP core already links.
- Scripts, one per step, chained by `port.sh`: `setup.sh` (fetch PSPRecomp, apply
  patches, install Emscripten, build tools), `extract_iso.py`, `decrypt.sh`
  (uses `pspdecrypt`), `generate.sh`, `manifest.py`, `build_web.sh`,
  `build_native.sh`, `serve.sh`.
- Recompilation entry point: `psp_recomp <module> --auto <out> 0x08804000 0x4000`.
  **Guests load at `0x08804000`** — the same base we derived by measurement for
  Shin Budokai Another Road. Generated code goes to `profile/generated/<game>` and
  is never committed.

Two titles so far, both God of War (Chains of Olympus, Ghost of Sparta), 55–60 fps
at three to four times native resolution in a browser. The second title needed the
PSP's DRM decryption for one small file, a handful of syscalls and a lighting fix —
that list is the honest size of per-title work on this route.

## 2. Why it belongs in our existing research, not beside it

It is the technique already recorded on 2026-10-10 for Budokai 3 Access and
NovaPowers' ReXGlue: **native code inside a statically recompiled console binary,
hooks placed at guest addresses** — the "third family" the survey
(`accessibility-mods-survey.md`) has no row for. What is new is that the host side
here is small, open and readable, where the Budokai 3 study had to work from a mod's
source with no game data on the host.

It also strengthens the survey's §2a argument ("hook the draw, do not poll for it").
In a recompiled build the guest's functions exist as host functions in generated
C++, so a reader calls or hooks the function that changes a selection rather than
scanning RAM for the selection — blind-starship's approach, with the symbol in hand.

## 3. What is genuinely worth taking

- **Symbols over addresses.** The strongest form of "ask the game what it knows":
  the hook target is a named function from the decompiled/recompiled code, not an
  address guessed from a RAM diff.
- **A harness shape worth copying regardless of the route.** The native runner is
  headless and scripted: `--press frame:buttons:length` holds PSP buttons from a
  given frame, `--stick frame:x,y:length`, `--dump-every N` writes PPM frames,
  `--wav` records the audio mix, `PSPWEB_TRACE_HLE=1` logs every syscall with
  arguments and result, `PSPWEB_DUMP_FRAME=n` dumps every primitive of one frame,
  and at the end it lists each guest thread with what it waits on, with a watchdog
  that reports where the guest spins. Our PSP harnesses drive through savestates;
  frame-exact scripted input with no savestate is a cleaner loop, and our
  `cue-field-drive.sh` / `cue-field-capture.sh` pair is the closest thing we have.
- **Vendored code we can reuse now.** `profile/third_party/at3_standalone` is
  PPSSPP's ATRAC3+ decoder packaged standalone with a pinned source commit — useful
  to us on its own, MIT-compatible packaging rules aside.

## 4. What it costs, and why it is not a backend

- **Per title, not general.** Generated C++ is per game, and the HLE layer needs
  per-title fixes. PPSSPP runs any PSP title today and our Dissidia adapter already
  reads through it. A recompiled core would be a new per-game port, not a replacement.
- **Browser-first target.** Emscripten/WASM, WebGL2, AudioWorklet, HTTP Range disc
  streaming. OGA needs cores on iOS, Android and the host with our speech, input
  bridging and announcement queue. `build_native.sh` proves a native build is
  possible; a phone build is unverified and nobody has done it.
- **No accessibility layer, no savestates, no readers.** Nothing of ours carries over.
- **Young.** Ten commits, one author, five days old, two titles, no releases.
- **Game data stays the user's**, which matches our rule and needs no exception.

## 5. Verdict

**Watch it. Build nothing on it yet.** Record the reuse candidates (the harness
shape, the standalone ATRAC3+ decoder, the symbol-level hook argument) rather than a
migration plan.

The cheapest honest experiment, if we ever pursue the route: build the project's
headless native runner for a PSP title we already read — Dissidia Final Fantasy
`ULUS10437` or Shin Budokai Another Road `ULUS10234` — and find out whether a
recompiled build boots far enough to be worth hooking. That is a measurement, not a
project, and it needs no accessibility work to answer.

⛔ **Not run, not measured:** the recompiler, the profile, the runner. Claims about
per-title effort and target platforms rest on the project's own README.
