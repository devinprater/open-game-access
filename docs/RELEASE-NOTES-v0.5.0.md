# Release notes — v0.5.0

The **Game Boy now works on iOS**, alongside the GBA that already did. This release
also pins the emulator tree that is still maintained, and registers the five consoles
that Mesen covers and this app could not name before.

⚠️ **If you are on v0.4.1 or earlier, replace it.** v0.4.0 crashed on the first frame
of every game; v0.4.1 fixed that. Nothing here regresses it.

## Game Boy / Game Boy Color on iOS — new

`.gb` and `.gbc` ROMs now load, run and are read aloud by the same Pokémon Access
reader that handles the GBA. The registry had claimed Game Boy was supported since
the registry landed, and the picker offered `.gb`/`.gbc` — but the iOS build defined
only `-DM_CORE_GBA`, so the Game Boy filter was compiled out of mGBA entirely. It did
not merely fail to handle the file, it did not **recognise** it. That is why a Game
Boy ROM answered "This game file is not a Game Boy or GBA ROM."

Two real bugs were hiding behind the missing flag, and both were found by booting a
ROM rather than by compiling one:

- **An SGB-enhanced Game Boy crashed on load.** The video buffer was set *before* the
  config was loaded, and loading the config rebuilds the renderer and clears that
  buffer to NULL. A Game Boy reset then draws the Super Game Boy border through it.
  Pokémon Blue segfaulted inside `_regenerateSGBBorder` every time. Fixed by ordering:
  config, then buffer, then reset.
- **A Super Game Boy frame is 256x224, not 160x144**, because the border is drawn into
  the frame — and the render target was sized 240x160. With the crash fixed, the border
  would have written about 2.8x past the end of that array. SGB borders are now off
  (which is what mGBA's own frontend does when it has no border surface), the target is
  sized for the worst case anyway, and the RGBA copy now walks row by row at the right
  stride — it used to copy one flat run, which is only correct for a 160-wide frame.

Measured after the fix, through the app's own code path, with the real reader:

| ROM | Result |
|---|---|
| Pokemon Crystal (`.gbc`) | boots, 160x144, 33 spoken lines — "My name is OAK." |
| Pokemon Blue, SGB Enhanced (`.gb`) | boots, 160x144, 17 spoken lines — "Hello there! Welcome to the world of POKeMON!" |

Both `RESULT: OK`. This is the first meaningful Game Boy speech this project has had.

## Android — fixed, and it was broken

Android's Game Boy list was a hand-written copy, and it **omitted `src/gb/mbc/mbc.c`**
— the dispatcher that holds the pointers into every other MBC file. Those files
compiled and nothing referenced them, so a Game Boy cartridge with a memory bank
controller (most of them, and every Pokemon) could not have been built for Android at
all. The Android list now reads the same generated lists iOS uses, so the two platforms
cannot disagree about what emulator they ship.

## Mesen: the maintained tree, and five more consoles registered

The Mesen pin pointed at `SourMesen/Mesen2`, which is **archived**. It now points at
**`nesdev-org/MesenCE`**, the maintained successor, and all seven of its console cores
were re-measured against that tree (508 translation units, zero failures).

The Master System / Game Gear, PC Engine and WonderSwan had **no row in the registry
at all** — the picker ignored those files entirely. They are now listed, with the right
extensions and pads, and the same is true of the SNES and NES rows, which now name a
real measured core.

**These five are not playable yet.** They are listed so the picker can *name* the
console instead of ignoring the file. There is no host glue for them; adding one is a
host file plus a build flag, and the source lists are already in the repo.

## What is verified, and how

- **The IPA**: unsigned (no `_CodeSignature`, no `embedded.mobileprovision` — what lets
  a sideloading tool re-sign it), targets `ios` and not `iossimulator`, and carries the
  Game Boy core symbols in the shipped binary.
- **The core archive was rebuilt**, checked for age and for its symbols — a stale
  archive links and then fails later with undefined `oga_*` symbols that read like a
  Swift problem.
- **Host suites**: the system registry (67 checks, 7 sabotages that must fail), the
  GBA/Game Boy adapter path, the announcement queue, build-flag parity, the Android
  Game Boy bridge (8 checks + 5 sabotages), and the core source-list check.
- **Real ROMs booted**: FireRed, LeafGreen, Emerald and Ruby on the GBA path (240x160,
  48 colours — the stride change did not disturb it), plus Crystal and Blue on the
  Game Boy path.

**Not verified on hardware.** All of the Game Boy work above is host-side, using the
same sources the device build compiles. The first run on the phone is yours.

## Downloads

| Platform | File | Notes |
|---|---|---|
| **iOS device** | `OpenGameAccess-v0.5.0.ipa` | **Unsigned** — re-sign with SideStore as usual. arm64, iOS 17+. |
| **iOS Simulator** | `OpenGameAccess-v0.5.0-simulator.zip` | macOS only to run. |
| **Android** | `app-gitHub-prod-debug.apk` | Built by CI on the release tag; attached here when it finishes (up to ~2 h after the release appears). |
