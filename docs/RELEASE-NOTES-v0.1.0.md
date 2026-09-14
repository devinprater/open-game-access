# Release notes — v0.1.0

First public release of **Open Game Access**: accessibility infrastructure for
mainstream games, built on semantic game-state inspection rather than pixels.

> **Model the game, not the screen.** When real game state is available, read it.
> OCR and pixel position are fallbacks, never the primary abstraction.

## Downloads

| Platform | File | Notes |
|---|---|---|
| **iOS** | `OpenGameAccess-simulator.zip` | iOS Simulator `.app`. Unzip, then `xcrun simctl install booted OpenGameAccess.app`. Requires macOS to *run*; the bundle itself is unsigned and needs no Apple account. |
| **Android** | `app-*-debug.apk` | Debug-signed, sideloadable. Install directly on the device. |

**On iOS device installs:** this release does not ship a ready-to-install `.ipa`.
Signing one requires a certificate and provisioning profile tied to a specific
Apple account, so a generic download would not install on your phone. The repo
documents the build instead — see `docs/building.md` and
`docs/device-access-wsl.md`.

## What works

### Pokémon Black / White (Nintendo DS) — mature

The original accessible player, unchanged. Ola's `main.lua` reader runs
byte-identical inside the melonDS core: it reads the game's own memory and
narrates dialogue, menus, the map, your party, and battles. The app is
VoiceOver-first — every DS button and every reading command is a labelled,
focusable element, and speech goes through one channel that routes correctly
whether VoiceOver is on or off.

Also in this build: the Game Boy / GBC / GBA reader set (Pokémon Crystal,
FireRed/LeafGreen, Ruby/Sapphire/Emerald, Gold/Silver/Crystal) via mGBA on Android.

### Fire Emblem: Shadow Dragon (Nintendo DS) — milestone 1

A new native adapter that reads the game's tactical state directly from its
structures — the first proof that this framework generalises beyond Pokémon.

```
Where am I?  -> Cursor 1, 20. Terrain: unknown (tile id not yet verified).
                Unit here: Marth, 18 HP, unacted.
Next ally    -> Marth, 18 HP, position 1, 20, unacted, 0.0 tiles away.
```

Character identity is read from the game's own identifier strings
(`PersonData -> 'PID_MARS'`, `JobData -> 'JID_LORD'`), not a hand-made table.

Verified against a running emulator:

- tactical cursor position, confirmed **two independent ways** (direct
  correlation, and pixel position ÷ camera tile size reproducing the tile
  coordinate across five snapshots)
- the unit array at `gUnitList`, record stride `0xA8` (**measured**, not the
  header's `sizeof`)
- level, HP, movement, X/Y, items, force pointer, and character/class identity

**Not yet implemented, and the adapter says so rather than guessing:** terrain
under the cursor, allegiance as a faction number, the acted-state bit, movement
and attack ranges, objectives, enemy cycling (the scripted test reaches a map
with only player units). Full detail and method in
`docs/fire-emblem-shadow-dragon-memory.md`.

## ⛔ This release contains no game data

No ROMs, no BIOS or firmware dumps, no save files, no patches derived from them.
You supply your own legally obtained game files. A CI check (`scripts/check-no-roms.sh`)
fails the build if any game data or emulator build output is found in the tree.

## Known limitations

- **The emulator core is not vendored.** It is fetched at a pinned revision by
  `scripts/bootstrap-deps.sh`, because melonDS is a separate GPL-3.0 project.
  Building from source is required for iOS; the Android APK is complete as shipped.
- **iOS device builds need your own Apple certificate.** See above.
- **No audio has been verified** on the iOS side — speech is confirmed at the
  callback boundary, not through a speaker.
- **The Fire Emblem adapter is not yet wired into the app's UI.** It runs as a
  host tool (`./wsl.sh fe-access`) while the state is being proven.
- Android: reinstalling wipes the app's ROM directory, so you re-pick your folder
  after each install.

## Licensing and credits

This project is **GPL-3.0-or-later** because it links against melonDS (GPL-3.0).
Bundled components keep their own licences:

- **melonDS** — the DS emulation core. GPL-3.0. <https://github.com/melonDS-emu/melonDS>
  - Lua-scripting fork: <https://github.com/NPO-197/melonDS-lua>
  - Android frontend basis: <https://github.com/rafaelvcaetano/melonDS-android>
- **Lua 5.4** — MIT. <https://www.lua.org/>
- **teakra** (DSi DSP) — MIT/CC0
- **mGBA** (Game Boy family, Android) — MPL-2.0. <https://mgba.io/>
- **xtool** (iOS build/sign without Xcode) — MIT. <https://github.com/xtool-org/xtool>

**Accessibility scripts.** The NDS and GBA Lua readers are the work of their own
authors, not this project — credit to Ola and the Pokémon Access project
(<https://github.com/nuive/pokemon-access>). If you are one of these authors and
want a different credit, licence or removal, please open an issue.

**Reverse engineering.** Every address in the Fire Emblem documentation comes from
the **fe11-us** decompilation by Eebit and contributors
(<https://github.com/Eebit/fe11-us>). Prior art for a tactics-game screen reader:
**StanHash/GBA-Fire-Embem-for-Screen-Readers**.

Accessibility design inspiration (approach only, no code copied): bdc_access,
Blindest-Dungeon, FtlAccess, VcmiAccess, SRWY-Accessibility, cultaccess,
SO2RAccess. Full list with links in the README.
