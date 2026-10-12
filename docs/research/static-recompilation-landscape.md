# The static recompilation landscape — what it is now, and what it is worth to us

Surveyed 2026-10-12, after `docs/research/psp-web-recomp.md`. **Nothing here was run.**
Sources are the GitHub API (repo metadata, trees, commit logs) and each project's own
README and docs. Everything unverified is marked. Where a claim rests only on a
project's marketing text, it says so.

Trigger: Devin pointed at `snuri00/psp-web-recomp`, then `snuri00/ps2-web-recomp`, and
asked what else is like them.

---

## 1. The family count changed again

`accessibility-mods-survey.md` found two families (in-process .NET mods; emulator-driven
readers) and `budokai3access-lessons.md` added a third (native code inside a statically
recompiled console binary). Since that write-up, static recompilation stopped being a
curiosity: **every major console now has a translator, most have a runtime, and several
have shipped player-facing ports.** The third family is now the largest body of new work
in this space, and its defining property is the one we lack — a game's own functions
exist by name in the compiled program.

Recompilation in one sentence, for the record: the console's executable is translated
into C or C++ ahead of time, then compiled for the host. No interpreter loop, no JIT, no
memory image to scan. The translator is generic per console; **the runtime is per game.**

## 2. The translators and runtimes

Console, project, license, size, and what it actually is:

**N64**
- `N64Recomp/N64Recomp` — MIT, ~8.2k stars. MIPS → C. The project that started this wave;
  explicitly inspired by the IDO static recompiler and jamulator (NES).
- `N64Recomp/N64ModernRuntime` — GPL-3.0, ~180 stars. The runtime for recompiled or
  traditionally ported N64 games.
- `Mr-Wiseguy/Zelda64Recomp` — GPL-3.0, ~7.4k stars. Majora's Mask shipped, Ocarina of
  Time "soon", "extensive mod support", mod template and modding documentation, RT64 renderer.
- ~15 more N64 game ports (Banjo, Star Fox 64, Mario Kart 64, Quest 64, Goemon, DKC,
  Mega Man 64, Pokémon Snap, Dinosaur Planet …) confirmed by name in the sweep.

**Xbox 360**
- `hedge-dev/XenonRecomp` — MIT, ~6.5k stars. PowerPC → C++, with `XenosRecomp` for shaders.
- `hedge-dev/UnleashedRecomp` — GPL-3.0, ~5.2k stars. Sonic Unleashed, Windows and Linux,
  March 2025, mod loader, installer, options menu.
- `rexglue/rexglue-sdk` — ~1.0k stars. "Xbox 360 Recompilation Runtime and Toolkit", built
  on Xenia's foundations. **This is the runtime Budokai 3 Access hooks.**
- `rexdex`'s older recompiler is credited as the first of its kind.

**GameCube / Wii / Wii U**
- `ExpansionPak/DolRecomp` — GPL-3.0, ~190 stars. The GameCube/Wii PowerPC → C translator,
  used by BlueWake with an added `--option-sites` mode.
- `chrissotraidis/bluewake` — GPL-3.0, ~330 stars, created 2026-09-28. **Wind Waker running
  natively on iPhone, iPad, Mac and Windows.** Dolphin-derived runtime for graphics, audio,
  memory cards and timing; translated code needs no JIT (rare instructions fall back to an
  interpreter); iOS 17+, A13 or newer; the player builds their own IPA on their own Mac from
  their own disc. Has a Builder with per-game profiles, and hook sites.
- `elliotttate/Wind-Waker-Recomp` — the earlier fork it merged from (GPL-3.0, ~525 stars).
- `ZeldaWWHDRecomp/ZeldaWWHDRecomp` — Wii U Wind Waker HD, releases to v0.2.7 (2026-10-08),
  Windows/macOS/Linux. Requires the player's own Wii U dump plus the console's own keys.
- `BlackLineInteractive/NWiiRecomp` — GameCube/Wii toolkit, ~29 stars, unverified.

**PlayStation 1**
- `RetroPortingToolKit/psxrecomp` — the busiest of the cluster (~296 stars, 72 forks);
  "Part of the R.A.I.D. community". `TombaRecomp` is its applied example (boots to gameplay
  with menus, FMV, audio, movement, combat — the project's claim).
- `Creesic/psxrecomp` and `seedlord/psxrecomp` — same design, different lineages (one
  recompiles the real SCPH1001 BIOS and runs it as the kernel, no HLE layer). License
  PolyForm Noncommercial. Its README states Claude Code was used as a pair programmer.
- `Mr-Wiseguy/PS1Recomp` is credited as an influence.

**PlayStation 2**
- `ran-j/PS2Recomp` — GPL-3.0, ~3.4k stars. Experimental, R5900 → C++ with
  `ps2xAnalyzer` / `ps2xRecomp` / `ps2xRuntime` / `ps2xIOP` and a TOML config.
- `snuri00/ps2-web-recomp` — MIT, ~210 stars, created 2026-10-07 (sibling of the PSP one).
  Gran Turismo 4 then God of War II, per the README; EE HLE kernel, the game's own IRX
  modules run on an R3000 interpreter, vector-unit microprograms compiled ahead of time,
  Graphics Synthesizer as WebGPU compute shaders, memory card in browser storage, native
  SDL runner as well as the page. Claims GoW II went ISO → page in 3.5 minutes.

**PSP**
- `jessicanataliagta/PSPRecomp` — MIT, ~220 stars. The framework `psp-web-recomp` patches.
- `snuri00/psp-web-recomp` — MIT, ~250 stars (see the earlier note).

**Original Xbox, 3DS, Switch**
- `sp00nznet/xboxrecomp` — MIT, ~183 stars, x86 XBE → Windows executables.
- `fearkov/3dsrecomp` — ~33 stars, ARM → C, created 2026-09-25, unverified.
- `bgyss/SwitchRecomp` — ~66 stars, Rust, "preservation-focused research", last pushed March 2026.
- `snuri00/ps3recomp` — 1 star, "static recompilation runtime libraries for PS3 titles",
  unverified and probably a placeholder.

## 3. Where our code can attach — the part that matters

Every framework in §2 has an intended place for outside code, and the place is a named
symbol rather than a RAM address. This is the mechanism difference from our emulator route.

- **XenonRecomp** documents the richest set, in its README: a translated function can be
  aliased to an "implementation function" and the original marked weakly linked, so a port
  can override a whole function while still calling the original, **and** "mid-asm hooks"
  can be inserted at specific instruction addresses, with chosen guest registers passed as
  arguments and optional return/jump control (`return`, `return_on_true`,
  `jump_address`, `after_instruction`). Declared in the port's TOML.
- **N64Recomp** offers "single file output mode", which emits every function into one file
  so a linker can substitute patched versions of named functions. Its `[[patches.func]]`
  and `[[patches.hook]]` TOML sections are documented as **planned but unimplemented** —
  do not plan around them. In practice the N64 mod API is the one Zelda64Recomp ships:
  mods are built with `RecompModTool` into `.nrm` files and the template's own README says
  the framework covers "how to write function patches and perform interop between mods".
- **ReXGlue** is the runtime under Budokai 3 Access, whose mechanism we already wrote up:
  hooks placed at guest PowerPC addresses resolved through the runtime's host pointer table.
- **BlueWake / DolRecomp** has two mechanisms worth naming. `--option-sites` translates each
  changed instruction both ways and picks a variant at run time from an option flag, so one
  translation serves every combination of settings. Its own mods are native C functions
  (`runtime/host/src/game_options.c`) "called from hook sites" in the translated code — for
  example turning the wind through the game's own `dKyw_tact_wind_set`. Code mods are
  compiled into the app, not loaded beside it.
- **PS2Recomp** has "Game Override Hooks": runtime-side, build-scoped patch modules, for
  per-game behaviour without polluting the global runtime.
- **snuri00's two web ports** have no hook story at all — but the host side is small,
  readable C++ (HLE kernel, GE renderer, devices), which is what we would be editing.

⛔ What is missing everywhere except Budokai 3 Access: **nothing reads state for a player.**
The hooks exist so ports can add features. Nobody has pointed them at speech.

## 4. The accessibility state of this scene

Searched, not exhaustively: GitHub repo and code search, AudioGames forum, and the
projects' own docs.

- **Budokai 3 Access** (`BlindChallengerX/Budokai3Access`, C, MIT, 2026-10-10) — on a
  ReXGlue port. Already written up.
- **blind-starship** (`ohylli/blind-starship`) — a fork of the StarFox 64 *decompilation*
  port, speech through Prism, spatial cues through Steam Audio, gameplay limited to
  training mode. Already written up.
- **Diablo Access** (`mojsior/diablo-access`) — new to us. A mod for **DevilutionX**, the
  Diablo source port: NVDA/JAWS through Tolk on Windows and speech-dispatcher on Linux,
  proximity cues for items, monsters, doors and chests, a tracker that speaks the target
  and directions, autowalk to tracked characters in town, an HP warning that speeds up as
  health falls, automatic monster-name announcement in attack range. The author states he
  is not a programmer and built it with Codex.
- **Silence — Accessibility mod** (`Lirin111/Silence---Accessibility-mod`) — a mod for a
  commercial 2D game, controller-first, hotspot cycling, and the author reports completing
  the game blind.

⛔ **No screen-reader mod exists for any statically recompiled port other than Budokai 3
Access.** Three of the four accessibility mods above attach to *source ports* or to native
PC games, which is the older, softer route: the code is there to read, but the port itself
is a project somebody else already finished.

## 5. What this does to our own plan

`docs/plans/emulator-agnostic-shell.md` records N64 as "needs a recompiler … ARM64 iOS
gives no JIT entitlement, so this is interpreter-or-nothing on sideloads", and PS2, GameCube
and Wii as "not soon". Recompilation changes the reason those rows are red:

- **The JIT wall is not a wall on this route.** BlueWake runs a GameCube game natively on
  an iPhone with no JIT, because the game is translated ahead of time and compiled for
  arm64. That is the same wall our PSP, DS and N64 cores hit. It applies to every console
  we run under an interpreter today.
- **But the price is one game at a time.** The translator is generic; the runtime, the HLE
  kernel and the per-title fixes are not. Two PSP titles, two PS2 titles, one GameCube
  title, one Xbox 360 title. Our PPSSPP core runs any PSP game and our adapters already
  read Dissidia through it.
- **And on the recompiled route the game is CODE, so it is one app per game.** BlueWake's
  own architecture doc states it: every target produces two components, a platform
  application and a translated game module, and the application loads the module into its
  own process from its own bundle. On iOS that module is compiled in at build time (the
  0.2.0 build record measures Wind Waker's at **502 MB**, 822 objects, and the project's
  publish gate refuses to release an IPA containing it as personal-only), which is why the
  player builds their own. An emulator inverts this — its core is generic and the game is
  only data, so one app holds any number of games. Ten games recompiled is ten apps, each
  built from its own disc, each hundreds of megabytes, each separately signed. Nothing in
  the technique forbids one app carrying several modules and picking between them; no
  project has done it, and it would not change the per-game port work.
- **Rule this implies:** take the recompiled route only when a port already exists for a
  game Devin wants, or when the console cannot be made to run acceptably on the target at
  all. Otherwise the emulator plus a reader stays cheaper, because the reader is the only
  part we would have had to write either way.

Per console, one line each:

- **N64** — the plan's blocker is real, and this route removes it; Zelda64Recomp is the
  worked example of a mod API we could build a reader on. Per game, and only for games that
  have a port.
- **GameCube / Wii** — BlueWake proves the target (iOS arm64, own disc, own IPA). One game,
  and one app for it; a second game means a second build and a second IPA.
- **Wii U** — ZeldaWWHDRecomp exists; requires console keys. Not our route.
- **PS1 / PS2** — translators exist and are young; PS2Recomp is explicitly experimental.
  Not near term for us, but this is the first time the row is not a hard no.
- **PSP** — PPSSPP stays the general core; a recompiled core would be a per-game port.
- **Original Xbox / 3DS / Switch** — no change; research-grade.

## 6. What is worth taking now, without a new project

1. **The hook designs, as a design vocabulary.** XenonRecomp's mid-asm hook and
   override-with-weak-linking model, ReXGlue's guest-address/host-table discipline, and
   BlueWake's option-sites are three worked answers to "how does our code meet the game's
   own code". Our adapter seam has a query shape and no push shape
   (`accessibility-mods-survey.md` §2a already flags this); these are reference designs for
   the push shape.
2. **The headless harness shape** from `psp-web-recomp` / `ps2-web-recomp`: scripted input
   by exact frame number, frame dumps, per-frame primitive dumps, `--wav` capture of the
   audio mix, syscall tracing, guest-thread listing, a spin watchdog. Our PSP harnesses
   drive through savestates; this is a cleaner loop and it needs no save.
3. **Diablo Access's cue set** as design material: proximity cue per category, a tracker
   that speaks the target and the direction to it, autowalk to a tracked thing, and a
   health warning that changes pace rather than repeating a word.
4. **The standalone ATRAC3+ decoder** vendored in `psp-web-recomp` (PPSSPP's, pinned commit).
5. **Nothing else.** No code of theirs is worth vendoring into a phone app.

## 7. Watch list, each with the trigger that makes it matter

- **BlueWake's Builder profiles** — if a *second* game profile appears, the pipeline is
  proven reusable rather than a Wind Waker-shaped one-off. That is the moment a GameCube
  reader becomes a real option instead of a fantasy.
- **ReXGlue + new "Access" mods** — Budokai 3 Access is the only precedent; a second one
  would make hooking recompiled ports a pattern we can plan around.
- **N64Recomp's `patches.func` / `patches.hook`** — currently unimplemented; when they land,
  a reader mod for any N64 port gets a supported door instead of a linker trick.
- **PS2Recomp / PSXRecomp reaching a playable title we want** — then the PS1/PS2 rows move.
- **`snuri00/ps3recomp`** — runtime libraries only, unverified.

## 8. Two honest warnings

- **This scene is now saturated with machine-generated single-author repositories.** One
  sweep returned 114 unique matches, many created within days, several with no license, and
  a large number that are one person's experiment with a confident README. Star counts in
  this space track novelty and hype rather than proof — the PSXRecomp README markets itself
  with how fast it was built and with what tooling. **Judge these projects by running them,
  never by their stars or their prose.** Our own two notes on `snuri00`'s pair rest on
  READMEs, and are marked as such.
- **The output is a translation of somebody's game code.** The projects say so themselves:
  BlueWake refuses to write an IPA inside its own repository, and both web-recomp projects
  say the generated C++ and WASM belong to the game's owners and must stay on the machine
  that built them. That is exactly our ROM rule, one step further along: we may build these
  things locally from Devin's own discs and must never publish them.

## 9. Bounded experiments, cheapest first

1. **Read, cost zero.** XenonRecomp's hook documentation plus one ReXGlue-based port's
   patch files, and BlueWake's `docs/MODS.md` and `docs/BUILDER.md`, as reference material
   for a hook-based reader design note. No game data needed.
2. **The PSP runner on a game we already read.** Build `psp-web-recomp`'s headless native
   runner for Dissidia Final Fantasy (`ULUS10437`, Devin's own disc) and find out whether a
   recompiled build boots far enough to be worth hooking. It answers "is this route real
   for us" with a measurement, and it needs no accessibility work to answer.
3. **BlueWake on Devin's own Wind Waker disc.** The build needs an Apple Silicon Mac,
   Xcode, and **25 GB free** — which the Mac build host does not currently have. Parked for
   that reason alone, and worth asking about later rather than now.

⛔ **Unverified:** none of the above was run on this host. The PSP and PS2 web-recomp
projects' performance and compatibility claims come from their READMEs.

## 10. The Hacker News thread around the PS2 port (read 2026-10-12)

`news.ycombinator.com/item?id=50052251` — Show HN for `snuri00/ps2-web-recomp`, 202 points,
108 comments, the author posting as `deadxwarrior`. Devin asked for the links in it. New to us:

- `cretz/vitaslop` (MIT, Rust, published 2026-07-11, ~10 stars) — **a PlayStation Vita
  emulator in a browser tab** at `cretz.github.io/vitaslop`, with the ARM-to-WASM transpiler
  itself compiled to WebAssembly. WebGPU, JSPI, shared memory and OPFS required, which means
  desktop and Android Chrome/Edge; **mobile Safari and mobile Firefox are explicitly not
  there yet**. The player adds their own games and they stay in the browser. Early: homebrew
  works, "some commercial games boot and play".
- `arcanite24/pinyon-shift` (BSD-3-Clause, ~390 stars) — Forza Horizon (Xbox 360) recompiled
  natively for Windows, Linux, Steam Deck and Apple silicon, with an Android alpha, built by
  the player from their own disc.
- `fangamesdb.com/consoles/xbox-360.html` — a catalogue of Xbox 360 recomp ports;
  `thegamingemporium.com` tracks the scene.
- `item?id=49991243` — the same treatment for the PSP project, when God of War was the news.
- A commenter is recompiling PS2 into Go rather than C++ to get SIMD support, aiming at a
  standalone player.

Author's own figures, worth keeping: the recompiler is ordinary C++ and builds for
WebAssembly easily, but **compiling its output is the heavy part — God of War II comes out
around 670k lines of C++** — and the cleaner design would be emitting WebAssembly directly
instead of going through a C++ compiler. He wrote it to play his own dumps over a VPN from
his own machine, and says plainly that hosting such a build publicly is not legal.

⛔ **A public URL cannot carry the game.** Any page holding translated game code is
distributing a game. Both web-recomp projects and BlueWake say the generated code belongs to
the game's owners and must stay on the machine that produced it. A public web address is only
legitimate when the player supplies their own disc and the translation happens on their own
machine — which is exactly the shape `vitaslop` took, with the transpiler itself running in
the browser.

## 11. Effort: recompile plus a reader, against an adapter

Devin's question: would recompiling a game and adding an accessibility mod be harder than
writing an adapter the way we do now? **Yes, decisively — because recompiling means doing the
emulator's job ourselves, per game, and then still writing the reader.** Three layers:

- **Make the game run at all.** The PSP web profile's host is **424 KB of C++ across 19 files**
  (`ge.cpp` 79 KB, `ge_gl.cpp` 75 KB, `kernel.cpp` 53 KB, `savedata.cpp`, `io.cpp`,
  `devices.cpp`, `atrac.cpp`, `sas.cpp`, `pgd.cpp`, `mpeg.cpp`, `webfs.cpp` …) for **one
  console**, before any per-title fix. And the per-title fixes are real: the first God of War
  needed DRM decryption for one file, several syscalls and a lighting fix; the second needed a
  hardware timer, two recompiler bugs, a culling fix, a boot path and a scratchpad DMA mode.
  Then there is the compile itself — 670k lines of generated C++ for God of War II.
- **Read the game.** Our adapters: Dissidia 1,877 lines, Another Road 780, DQ9 509, GBA 291,
  DBZ 280, FE 179, NES 119 — about 4,100 lines in total, on top of a Ghidra map (6,551
  functions for one PSP title) and live RAM verification. **Do not read that small code size
  as a small job:** the Dissidia adapter's 1,877 lines sit on a **9,656-line reverse-engineering
  write-up in 124 sections**, 11 of whose claims are still marked unverified, and the repo's
  reverse-engineering and research docs together are 28,102 lines. The code is small; the
  knowledge under it is the work. **This work does not disappear on the recompiled route —
  only the addresses become names.** A symbol tells you where a function is, never what its
  fields mean, which is the same information we dig out of live RAM today.
- **The app.** Speech, the announcement queue, braille, input bridging, the readers' UI. We
  already have it. A browser build has no equivalent to any of it.

So the comparison is not "adapter versus mod". It is "adapter on a core somebody else
maintains" against "our own per-game core, plus the adapter, plus the front end". PPSSPP,
mGBA, melonDS and Mesen are the part we never pay for today.

### But the reader cost cuts the other way, and our own note proves it

Of the Dissidia write-up's **124 sections, 31 have a correction, a falsification, a negative
result or an instrument fault in their own title.** There are **five separate instrument
faults**, an *intermittent* debugger input injection that invalidated a whole batch of
"D-pad inert" conclusions, two frozen-emulator false negatives, a delivery check that passed
trivially on an animating scene, and retractions of sections 17, 70 and 106. "Instrument
fault" appears 7 times in the file, "artefact" 12, "frozen" 31. **A large share of those 9,656
lines is spent fighting our own measuring instrument rather than on the game.**

That is precisely the class of work a recompiled build deletes. The game's own code is
readable, the hook target is a named function, and there is no debugger connection to lie to
us: no input-injection gate to validate, no watchpoint that freezes the emulator, no question
of whether the press arrived.

**So the honest answer to "would recompiling be easier" is conditional, and the condition is
not the technique:**

- **A port already exists for the game** (BlueWake, a shipped recompiled title): hooking it
  is *easier* than writing an adapter. The translation and the runtime are already paid for,
  discovery collapses to reading the generated code, and what remains is the reader and the
  app.
- **No port exists**: the port is the price of admission, and it dwarfs the reader.

The question to ask about a game is therefore **"does a port exist for it"**, not "which
technique is better" — and that is what turns the watch list in §7 from curiosity into a
trigger.

**Where recompilation wins anyway:** a console with no viable core on the target. N64 on iOS
is the case our own plan names, because there is no JIT entitlement; GameCube and PS2 on a
phone are the others. There the adapter route is not more expensive, it is unavailable.

**On web distribution specifically:** a URL is easier to hand out than a sideload, no
re-signing and no Mac, and the scene is moving that way (`vitaslop`, Forza Horizon's Android
alpha). But for Devin it costs exactly the things he uses: **the web has no braille API**, so
three braille displays are unserved; speech becomes `speechSynthesis` rather than VoiceOver or
NVDA; the announcement queue and every reader would need a second front end; and mobile Safari
lacks the WebGPU/JSPI/shared-memory support these pages need, so the phone, which is his main
target, is the weakest browser. And it cannot be hosted with the game in it (§10).
