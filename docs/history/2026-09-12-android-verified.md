# Pokémon Access — final verified state

## ✅ VERIFIED WORKING (independently, by the parent agent — not a subagent's word)

### Android app: the accessibility script runs and speaks
APK: `C:/Users/Devin Prater/AppData/Local/Temp/pokemon-a11y-app/native/melonDS-android/app/build/outputs/apk/gitHubProd/debug/app-gitHub-prod-debug.apk`
(55,089,417 bytes, package `me.magnum.melonds.dev`)

Live emulator session, captured directly:

```
PokemonAccess: [pokemon-access] speech bridge installed
PokemonAccess: [pokemon-access] assets: lua/bizhawk_compat.lua (6538 chars), lua/main.lua (2124264 chars)
PokemonAccess: Pokemon black accessibility loader running.
PokemonAccess: [ctx] the ZoneDataSystem is unreadable, ...   <- script's own dev trail,
              proof its MainRAM reads execute against the live console
PokemonAccess: [pokemon-access] accessibility script loaded (shim + main.lua, 2130803 chars)
PokemonAccess: [speech] say: Pokemon black accessibility loader running.
GoogleTTSServiceImpl: Synthesis request for locale eng-USA
GoogleTTSServiceImpl: TTS dispatch: en-us-x-iog-seanet-embedded
```

- **Lua assets ARE in the APK**: `unzip -l` lists exactly two —
  `assets/lua/bizhawk_compat.lua` (6547) and `assets/lua/main.lua` (2139602).
  Before the fix the same command returned ZERO lua entries.
- **`main.lua` is byte-identical to Devin's original.** sha256 of the file
  extracted straight back out of the APK == sha256 of
  `Dropbox/Games/NDS/Lua/main.lua` ==
  `abb737844a5ef78b6b1394ab3e2febc7492247b3ab3cbd6420cca9dd58e2649c`. Never edited.
- **The game runs in the app**: screenshot shows Reshiram + the
  "Pokémon BLACK VERSION" title screen with the on-screen controls.
- Lua is statically linked into `lib/x86_64/libmelonDS-android-frontend.so`
  (symbols `lua_resume`, `luaL_loadbuffer`, `luaL_newstate`, `hermes_tts`).

### Core: Pokémon Black is IN-GAME (it was never broken)
After ~20,000 frames of firmware boot it renders the player's bedroom — isometric
room, wallpaper, window, bed, rug, bookshelf, drawers, plant, table, two sprites.
Framebuffer hashing shows colours climbing 1 → 10 → 21 → 256, `VRAMCNT_A` going
00 → 81 → 83, and 13 of 18 frame hashes differing (i.e. animating).

### Core: the memory API reads real game memory
Script vs C++ ground truth, same instant:

| | script | C++ |
|---|---|---|
| nonzero bytes (4 MiB) | 2,385,237 | 2,169,819 |
| `[0x02004000]` | FF | FF |
| `[0x02004004]` | FF | FF |
| `[0x02080000]` | 30 | 30 |
| `[0x02200000]` | 00 | 00 |

## The four real bugs fixed

1. **DS keys are ACTIVE LOW and `SetKeyMask()` does not invert.** Passing a
   pressed-bits mask (0 when idle) reported EVERY BUTTON HELD forever, so no game
   could pass its boot input handling. Fix: `released = 0x00000FFF`, clear a bit
   per pressed button. Measured: `VRAMCNT_A` 00 → 82/83, Main RAM code +608 KB.
2. **`NDS::Reset()` never called** → every memory-timing table zero → one frame
   never completed (>300 s/frame). After the fix: 212 fps (353% of realtime).
3. **The memory API compared a guest ADDRESS against a domain SIZE.** `0x02000000`
   vs 4 MiB → the bounds check failed for every real address → every read returned
   0 silently, for every domain. The script loaded, spoke its one loader line, and
   then narrated nothing forever. Fix: give each domain a `start` and rebase
   (`off = address - start`) in the scalar read, the scalar write, AND
   `read_bytes_as_array` — each had its own copy of the bug.
4. **Missing second `Reset()` after cart insertion**, plus unseeded PowerMan/RTC
   (the frontend must supply both) and the JIT-requested trap.

## ⛔ What is NOT verified

- **No one has heard the audio.** Speech is confirmed at the TTS-synthesis layer
  (Google TTS bound, synthesis request, voice dispatched) and in the app's own
  speech log. The emulator's audio output was not capturable from this session.
- **In-game accessibility content is not demonstrated.** The only spoken line is
  the loader line. Narration is on-demand (hotkeys) and gated on game state, and
  the game was not driven past its title screen inside the app.
- The script reports "[ctx] the ZoneDataSystem is unreadable" — main.lua's own
  diagnostic for one subsystem in this ROM state, not a port defect, but it does
  mean place names and routing are stood down until it resolves.

## Two measurement errors of mine, corrected (kept as warnings)

- **"Black stalls" was WRONG.** Sampling `ARM9.PC` once per frame lands at
  END OF FRAME, where a game idles waiting for VBlank, so the PC looks parked
  forever. Hash the framebuffer instead.
- **The first "memory reads return 0" probe was inconclusive** — it ran before
  boot when Main RAM is genuinely empty, and both sides read true zeros. The real
  bug was found by a bogus-domain read that *succeeded*.
