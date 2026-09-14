# Current architecture (as of 2026-09-13)

This describes the code as it actually is, so that `open-game-access` can be
factored out of it incrementally instead of being rewritten up front. Everything
below was read from the tree, not remembered.

## The tree today

The working project is `C:\Users\Devin Prater\open-game-access` — a SwiftUI iOS
app around a melonDS core with a Lua accessibility script. Its name is now a
misnomer: the machinery under it is not Pokémon-specific (details in
"What is actually reusable").

```
open-game-access/
  Core/
    pokecore.cpp          the C++ integration layer: emulator lifecycle, the Lua
                          bindings, input, speech/log plumbing  (~1100 lines)
    poke_platform.cpp     melonDS's Platform:: API implemented for iOS/host
    poke_platform.h       the Platform log-forward hook
    probe.cpp, fedump.cpp,
    screen.cpp, ramwatch.cpp …   host-side reverse-engineering instruments
  Sources/
    CPokeCore/include/pokecore.h   the C ABI Swift is allowed to see
    PokemonAccess/
      GameSession.swift   owns the core, drives the frame loop (CADisplayLink)
      SpeechEngine.swift  the single speech channel (AVSpeechSynthesizer/VoiceOver)
      InputBridge.swift   UI -> core input, ROM store, bundle resources
      RootView.swift      VoiceOver-first control surface
      SettingsView.swift  settings + the reading log
      Resources/          bizhawk_compat.lua + main.lua (bundled, byte-identical)
  scripts/                100+ build/analysis scripts, all via wsl.sh <name>
  wsl.sh                  the single WSL entry point (mirrors sources, keeps builds)
```

## 1. What emulator integration exists?

**melonDS**, vendored as the NPO-197/melonDS-lua fork (the Lua-capable one),
compiled to a static archive by `scripts/build-core.sh` and force-loaded into the
app (`Package.swift` uses `-force_load` because nothing in Swift takes the address
of a `poke_*` symbol).

- Two archives today: `Vendor/libpokecore.a` (device, `arm64-apple-ios17.0`) and
  `Vendor/sim/libpokecore-sim.a` (simulator). Both from one shared source list
  (`scripts/core-sources.sh`) so they cannot drift.
- 113 translation units: melonDS core + Lua 5.4.7 + teakra (DSi DSP) + the two
  glue files.
- The JIT is deliberately **off** (`args.JIT = std::nullopt`): the ARM64 JIT needs
  `MAP_JIT`/`pthread_jit_write_protect_np`, which iOS grants only to apps signed
  with the `dynamic-codesigning` entitlement. The interpreter runs ~353% of
  realtime on a modern host, so this costs nothing.
- Boot configuration, learned the hard way: `Reset()` → `SetNDSCart()` → `Reset()`
  → seed PowerMan + RTC → `SetupDirectBoot()` when needed. Real BIOS/firmware is
  *not* used (a real firmware dump hangs in the BIOS; both the Android port and
  this app boot on FreeBIOS + generated firmware → direct boot).

## 2. How are DS RAM reads performed?

`Core/pokecore.cpp` implements the Lua `memory` library against the live
`melonDS::NDS`:

- Domains come from `DomainsFor(core)`; each has a `name`, a guest `start`
  address, a `size`, and a reader that either walks the CPU bus or reads a flat
  buffer.
- **The bounds check rebases the guest address by the domain's `start`**
  (`off = address - domain.start`) before comparing against `size`. Getting this
  wrong made every read above 4 MiB silently return 0 — one of the four real bugs
  found on this project.
- `ResolveDomain` accepts `"Main RAM"` and `"MainRAM"` (the shim writes one, the
  fork's docs use the other) by stripping spaces/underscores before comparing.
- Scalar reads/writes and `read_bytes_as_array` each had their own copy of the
  rebase bug; all three are fixed.
- Bus reads are guarded against addresses with hardware side effects (key input,
  touch, IPC FIFOs) — reading them through the bus mutates state.

From the host, reverse-engineering tools read `nds->MainRAM` **directly** (a flat
4 MiB buffer at guest `0x02000000`) rather than through the bus. That is the right
call for analysis: it cannot perturb the emulated machine.

## 3. How are addresses/signatures located?

Three techniques are in use, in order of preference:

1. **Published symbol maps.** For Fire Emblem: Shadow Dragon the
   `Eebit/fe11-us` decompilation ships `config/YFEE01/arm9/symbols.txt` — ~9,000
   named functions and data symbols with real addresses. This is the single most
   valuable input and it is used directly (see `docs/fire-emblem-shadow-dragon-memory.md`).
2. **Structural walking, not diffing.** `Core/fedump.cpp` follows
   `gMapStateManager → cursor` and `gUnitList → Unit → next` using the class
   layouts from the decompilation's headers. Diffing was tried first and is
   strictly worse: it finds *changed* bytes, and a live game rewrites thousands of
   them per second (frame counters, audio, RNG, VBlank flags).
3. **Published cheat codes as leads.** Action Replay codes are absolute RAM
   addresses. They are treated as claims to verify against a live console, never
   as facts — see `docs/tier1-research-status.md` for a case where a whole
   published list read zero because it was tagged for a different region.

`Core/probe.cpp` is the general instrument: it runs a timed plan of button presses
and takes paired checkpoints — `SHOT` (PPM screenshot) and `SNAP` (full Main RAM
dump). The pairing is the whole point: a RAM diff means nothing without a picture
of what the game was doing between snapshots, and a screenshot cannot tell you
where a value lives. `scripts/ramdiff.py` ranks candidate addresses by *signal
shape* (e.g. `--small-pair`: two adjacent values in 0..40 that both changed — what
a cursor moving one tile produces).

## 4. How are keyboard commands handled?

Two distinct paths, and they are not the same thing:

- **DS buttons** — `poke_set_button(core, ds_button, down)` sets a bit in
  `core->buttonsDown`; `ApplyInput()` (called at the top of every `poke_frame`)
  converts it to the console's **active-low** key mask and calls `SetKeyMask`.
  ⛔ DS keys are active low and `SetKeyMask` does *not* invert: bit = 1 means
  RELEASED. Passing a pressed-bits mask reports every button held forever, and the
  game silently never boots. The mask is built as all-released with a bit cleared
  per pressed button.
- **Accessibility hotkeys** — `poke_set_hotkey(core, "K", down)` appends/removes a
  letter in `core->hotkeysDown`, which the Lua `input.HeldKeys()` binding exposes.
  The script reads them through its own `input.get()` on **each frame**, and
  edge-detects them itself — so a UI button must deliver a press and a release a
  frame or two apart. `InputBridge.tapHotkey` does exactly that (0.08 s hold).
- **`joypad.set{}` overrides** — a third path added so the script's controller-mod
  layer works: `LuaJoySet` records a per-frame override mask, `ApplyInput` applies
  it *after* the physical state and then clears it (self-limiting; the script
  re-asserts each frame). Verified by reading the console's own `KeyInput`.

## 5. How is speech handled?

One channel, `SpeechEngine.swift`, fed by the core's speech callback:

- `poke_set_speech_callback(core, cb, userdata)`; the callback signature is
  `(const char* utf8_text, bool interrupt, void* userdata)`.
- **A NULL `text` means "stop speaking", not "nothing to say."** Swallowing it
  leaves the player with no way to silence the game — that was a real bug here.
- `interrupt == false` means "queue this after whatever is speaking".
- When VoiceOver is running the text goes through `UIAccessibility.post(.announcement)`
  instead of `AVSpeechSynthesizer`, because VoiceOver owns the audio session and a
  second synthesiser talking over it cuts both off mid-word. With VoiceOver off
  the synthesiser is used directly (and keeps working with the screen off).
- A silence latch (`isStopped`) makes "stop speech" *stick*: the script's readers
  produce lines every frame, so merely stopping the current utterance means it is
  talking again one frame later. Non-requested lines are dropped while latched; a
  line the player asked for (interrupt-style) lifts the latch.
- The Lua side is a global named `hermes_tts` with `speak(text, mode)` /
  `stop()`, kept from the Android port so the shim text is identical on both.
- The script's own dev trail goes to `print`, which `LuaPrint` forwards to the
  app's log callback and into the Reading Log in Settings.

## 6. What is Pokémon-specific vs reusable?

**Reusable as-is (game-agnostic):**

| Component | Notes |
|---|---|
| `poke_platform.cpp` | melonDS `Platform::` implementation — nothing game-specific |
| The `memory` Lua library + rebase logic | generic guest-address → domain access |
| `probe.cpp`, `ramwatch.cpp`, `ramdiff.py`, `ppm2png.py` | general RE instruments |
| `scripts/build-*.sh`, `core-sources.sh`, `wsl.sh` | build plumbing, no game knowledge |
| SwiftUI shell: ROM picker, screen view, audio, savestates | ROM-format agnostic |
| `SpeechEngine.swift` | the whole speech contract is generic |
| `InputBridge` button/hotkey plumbing | the *hotkey set* is script-specific, the mechanism is not |

**Script/adapter-specific (must move behind an adapter boundary):**

- `GameSession.bundledScript` hard-codes `bizhawk_compat.lua` + `main.lua`.
- The bundled `main.lua` is Ola's Pokémon reader: 23,163 lines, 33 screen modules.
- The `RootView` "Reading commands" group names Pokémon hotkeys (K/J/L/I/O/C/P).
- `LuaRead`/`LuaWrite`/`LuaGetJoy`/`LuaNDSTapUp` etc. are shaped to the BizHawk
  compatibility surface that `main.lua` expects.

**The seam that already exists** is the C ABI (`pokecore.h`) plus the Lua binding
surface. A game adapter therefore has two viable shapes and the project does not
have to choose now:

1. **Lua adapter** (what Pokémon uses): an adapter ships its own Lua script; the
   core stays unchanged. Cheapest to add, but the adapter's logic runs inside Lua
   with the same memory API.
2. **Native adapter**: a C++ module in `Core/` that reads structures directly and
   exposes semantic queries over the C ABI. Needed for anything the Lua surface is
   too slow or too coarse for (e.g. walking a unit linked list every frame).

Fire Emblem: Shadow Dragon is being built as a **native adapter** (`Core/fedump.cpp`
is its first form) because the tactical state is a pointer graph, and because the
decompilation gives us real struct layouts to read against.

## 7. Can a second adapter load without destabilising Pokémon?

**Yes, and this is the important finding.** Nothing in the adapter path is
entangled with Pokémon:

- `poke_load_rom` builds a fresh `NDS` per ROM, and `poke_create`/`poke_destroy`
  are per-instance — there is no global emulator state to fight over.
- The one global is `gCurrentCore` in `pokecore.cpp`, used only by the Platform
  layer's stop signal, and it is set/cleared by the same core that owns it.
- The script is per-core (`core->script`), so a Fire Emblem session simply never
  calls `poke_set_script` with the Pokémon reader.
- The frame loop, speech callback and log callback are all per-core pointers.

So an adapter can be selected **at ROM-load time** with no change to the Pokémon
path: detect the game code from the ROM header (`IRBO`/`IRAO` for Black/White,
`YFEE` for Shadow Dragon) and choose the reader. The risk is not correctness, it
is *complexity* — hence keeping adapters behind one boundary rather than spreading
game checks through `pokecore.cpp`.

**Not yet factored out, and deliberately so:** the directory reorganisation in the
brief. The build works, the tests work, and the emulator integration works; moving
100+ scripts and a Swift package into a new tree before Fire Emblem reads state
would risk all three for no functional gain. The concrete boundary to extract
first is the adapter seam described above.

## Next step

`docs/fire-emblem-shadow-dragon-memory.md` records what has been verified against
the running game, including what is still uncertain.
