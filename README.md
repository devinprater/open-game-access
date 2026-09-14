# Open Game Access

Accessibility infrastructure for mainstream games, built on **semantic game-state
inspection** rather than pixels.

The goal is to expose otherwise inaccessible games through screen-reader speech,
structured review interfaces, spatial audio, navigation assistance, and safe input
automation — by asking the game what it already knows.

> **Model the game, not the screen.**
> When real game state is available, read it. OCR and pixel position are fallbacks,
> never the primary abstraction.

## What this is now

This began as `pokemon-access-mobile` — an accessible Pokémon Black/White player
for Android and iOS. It is being refactored **incrementally, not rewritten**, into a
general framework with game-specific adapters. The working Pokémon code is
untouched and still runs.

| Piece | State |
|---|---|
| Emulator integration (melonDS + Lua 5.4, iOS build + host build) | working |
| Memory access, speech engine, VoiceOver routing, input bridging | working |
| Pokémon Black/White reader (Ola's `main.lua`, 33 screen modules) | working |
| Fire Emblem: Shadow Dragon adapter | **milestone 1 reached** |
| Adapter seam (`Core/adapter.h`) | defined; keyed on ROM game code |

### Fire Emblem: Shadow Dragon, milestone 1

Read from the game's own structures, verified against a running emulator:

```
Where am I?  -> Cursor 1, 20. Terrain: unknown (tile id not yet verified).
                Unit here: Marth, 18 HP, unacted.
Next ally    -> Marth, 18 HP, position 1, 20, unacted, 0.0 tiles away.

gUnitList 0x0227527C  stride 0xA8   slot 1 @ 0x02275328   Lv 1  HP 18  at (1,20)
  PersonData -> pid 'PID_MARS'      JobData -> jid 'JID_LORD'
```

Character identity is read from the game's own identifier strings, not a hard-coded
table. The cursor is confirmed two independent ways (direct correlation, and pixel
position ÷ camera tile size reproducing the tile coordinate across five snapshots).
What is verified, what is not, and the method for each:
[`docs/fire-emblem-shadow-dragon-memory.md`](docs/fire-emblem-shadow-dragon-memory.md).

## ⛔ No ROMs, ever

**This repository contains no game data.** No ROMs, no BIOS or firmware dumps, no
save files, no patches derived from them. You supply your own legally obtained game
files.

`scripts/stage-repo.sh` enforces this: it assembles the publish tree from an
allow-list of paths and then **fails the build** if anything matching a ROM, save,
BIOS, firmware or emulator object file is found in it. `.gitignore` is a second
line of defence, not the first.

## Downloads

Builds are published on the [Releases](../../releases) page.

| Platform | Artifact | Status |
|---|---|---|
| **iOS** | `open-game-access-ios-simulator.zip` — iOS Simulator `.app` | built in CI |
| **iOS** | device `.ipa` (arm64, sideload) | built on a machine with an Apple Developer certificate; see below |
| **Android** | `app-*.apk` (arm64-v8a / armeabi-v7a / x86_64) | built in CI |

**iOS device builds are not published as a ready-to-install `.ipa`.** Signing an
`.ipa` for a physical device requires an Apple Developer certificate and
provisioning profile belonging to *your* account, so a generic download would not
install on your phone anyway. Instead the workflow documents the exact steps
(`docs/building.md`) and CI produces the unsigned app for inspection. The simulator
build needs no signing at all and is the artifact CI ships.

## Building

See [`docs/building.md`](docs/building.md). Short version:

```bash
# iOS (Linux/WSL or macOS): fetches and builds the core, then the app
./scripts/bootstrap-deps.sh          # fetch melonDS-lua + Lua 5.4 + Apple SDK notes
./wsl.sh build-core                  # device core archive
./wsl.sh build-sim-app               # iOS Simulator .app

# Android
cd app && ./gradlew :app:assembleGitHubProdDebug
```

The emulator core is **fetched, not vendored** — see below.

## Third-party components and attribution

This project is a front-end and an accessibility layer. Nearly all of the emulation
is other people's work, and their licences apply.

### melonDS — Nintendo DS emulation core
- Upstream: <https://github.com/melonDS-emu/melonDS> — **GPL-3.0-or-later**
- Lua-scripting fork used here: <https://github.com/NPO-197/melonDS-lua> (based on
  the upstream Lua support proposed in [PR #1671](https://github.com/melonDS-emu/melonDS/pull/1671))
- Android frontend (the basis of the Android port):
  <https://github.com/rafaelvcaetano/melonDS-android> — GPL-3.0
- melonDS is Copyright the melonDS team. It is licensed under the GPL and is
  redistributed/derived here under the same terms.

### Lua 5.4
- <https://www.lua.org/> — **MIT licence**
- Copyright © 1994–2024 Lua.org, PUC-Rio.

### teakra (DSi DSP emulator)
- <https://github.com/nds-emulator-SDK/teakra> — **MIT licence / CC0**
- A hard dependency of melonDS's DSi DSP support.

### mGBA — Game Boy / GBC / GBA emulation core
- <https://mgba.io/> / <https://github.com/mgba-emu/mgba> — **MPL-2.0**
- Used by the Android build for the Game Boy family of scripts.

### Apple SDK / Swift
- The iOS build uses Apple's Darwin SDK, obtained via your own Apple Developer
  account. See `docs/building.md` and the `xtool` notes below.

### xtool — cross-platform Xcode replacement
- <https://github.com/xtool-org/xtool> — **MIT licence**
- Builds and signs the iOS app from Linux/Windows without Xcode.

### Accessibility Lua scripts

⛔ **These are the user's own work and are the reason the project exists. They are
NOT third-party code — credit and licence are the author's.** Attribution here is
by request and is not a claim of ownership by this project:

- **Nintendo DS reader** — `main.lua`, the "Pokémon Access" loader. Bundled at
  `Sources/PokemonAccess/Resources/main.lua` (byte-identical to the original,
  sha256 `abb73784…649c`). Its header credits Ola and the Pokémon Access project.
  The desktop predecessor and the sibling Game Boy/GBA branch:
  <https://github.com/nuive/pokemon-access>
- **Game Boy / GBC / GBA readers** — the `lua/gb/` script set (Pokémon Crystal,
  FireRed/LeafGreen, Ruby/Sapphire/Emerald, Gold/Silver/Crystal, and the shared
  `gb.lua` / `gba.lua` / `a-star.lua` / `serpent.lua` helpers). Same project as
  above; credited to Ola and contributors.
- **BizHawk compatibility shim** — `bizhawk_compat.lua`, written for this project
  to map the BizHawk Lua surface onto melonDS-lua. MIT, by this project.

If you are one of these authors and want a different credit, licence or removal,
please open an issue.

### Reverse-engineering references

Documentation, not code, but this project would not exist without it:

- **fe11-us** — Fire Emblem: Shadow Dragon decompilation by Eebit and contributors:
  <https://github.com/Eebit/fe11-us>. Its `symbols.txt` and class headers are the
  source of every address in `docs/fire-emblem-shadow-dragon-memory.md`.
- **Fire Emblem Universe** FE11 documentation thread:
  <https://feuniverse.us/t/fire-emblem-shadow-dragon-fe11-documentation/27666>
- **StanHash/GBA-Fire-Embem-for-Screen-Readers** — prior art for a tactics-game
  screen reader: <https://github.com/StanHash/GBA-Fire-Embem-for-Screen-Readers>

### Accessibility design references

Studied for approach; no code copied:

- [president-lion/bdc_access](https://github.com/president-lion/bdc_access) — semantic
  categories over a point-and-click game
- [Vicorin/Blindest-Dungeon](https://github.com/Vicorin/Blindest-Dungeon) — live
  memory reading, event log
- [Makenann/FtlAccess](https://github.com/Makenann/FtlAccess) — structured review
  layer for a real-time game
- [HappyStarfish/VcmiAccess](https://github.com/HappyStarfish/VcmiAccess) — review
  cursor independent of movement
- [stephenso1120-cmyk/SRWY-Accessibility](https://github.com/stephenso1120-cmyk/SRWY-Accessibility)
  — tactical-map accessibility in a strategy RPG
- [zersiax/cultaccess](https://github.com/zersiax/cultaccess) — semantic filters,
  earcons, autowalk
- [Yakku5226/SO2RAccess](https://github.com/Yakku5226/SO2RAccess) — navigation modes
  for a free-roaming JRPG

## Licence

The code in this repository is licensed under the **GNU General Public License
v3.0 or later**, because it links against melonDS, which is GPL-3.0. See
[`LICENSE`](LICENSE). Where a bundled component carries a different licence
(Lua 5.4 — MIT; teakra — MIT/CC0; mGBA — MPL-2.0), that component's licence governs
it; those are listed above.

The accessibility Lua scripts have their own authorship — see the attribution
section.

## Design rules

**Speech is for meaning; audio is for geometry and time.** Names, HP, terrain, menu
text and objectives are speech. Bearing, distance, danger and arrival are eventually
earcons — not a voice reading numbers continuously.

**Review must not equal action.** The player can inspect the world without
committing to a move.

**Use the game's real actions.** Send normal inputs and drive the real cursor; do
not teleport units or mutate gameplay state. The accessibility layer exposes the
game; it does not replace its rules.

**Read-only until there is a clear reason not to be.** No RAM writes anywhere so far.

**Fail safe.** Every read is bounds-checked and every pointer range-validated. If
expected state is missing, the feature stands down and says so — it does not crash
the emulator and it does not read garbage at the player.
