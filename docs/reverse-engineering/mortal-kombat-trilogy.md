# Mortal Kombat Trilogy (PlayStation, USA, SLUS-00330)

Reader: `Core/mkt_adapter.cpp`, map: `Core/mkt_adapter.h`. Started 2026-11-25.

## Where it stands

- The disc's serial selects the reader (registered in `Core/adapters.cpp`, game code `SLUS00330`).
- Shipped map is EMPTY: every cue is off until its address is measured on a live game.
- Written against that map, off for now: health beeps and milestone lines (the MKU language),
  the closeness beacon (CueSnapshot locked/dist, silent while paused), a "Foe on your left/right"
  line when the foe crosses over, and the pause menu ("Paused." + highlighted row, then each row).
- Where Am I says "Not mapped yet in this game." DumpState logs the map's state, the GameShark
  candidates below, and an anchor-word scan of all 2 MiB of main RAM.
- New debug channel command for mapping: `search` (see `docs/design/debug-channel.md`).

## Not done yet

- Host test for the adapter (`Core/mkt_adapter_test.cpp`, with a fake map via `MktSetMap`).
- Android: PS1 session has no debug channel folder, adapter readiness poll, reader buttons or cue
  poll/synth mix yet (Ps1SessionJNI.cpp, Ps1AccessibilityScript.kt, Ps1GameSession.kt, launcher).
- iOS/Android command and setting tables (`AdapterCommand.swift`, `tools/android-library-gen.cpp`,
  both `GameSettings`) have no `mkt` row yet.
- The measured map itself.

## Candidate addresses (published GameShark codes, NOT measured)

Source: almarsguides.com MKT GameShark pages (NTSC-U). Two pressings share the serial.

| What | v1.0 | v1.1 |
| --- | --- | --- |
| P1 health (halfword, full 0xA6) | 0x8003219E | 0x80032146 |
| P2 health | 0x80032316 | (P2 page not read) |
| P1 run meter (full 0x30) | 0x800321A0 | |
| P1 round wins | 0x800321A4 | |
| P2 run meter / wins | 0x80032318 / 0x8003231C | |
| P1 / P2 character id (byte) | | 0x800282E2 / 0x8002832E |

v1.0 P1 and P2 blocks are 0x178 apart. Confirm each by `peek`/`watch` in a fight before it goes
into the map.
