# The in-game debug channel

WHY THIS EXISTS: Budokai 3 Access (mined in `docs/research/budokai3access-lessons.md` §6)
has a `cmd.txt` file its author edits while the game runs: count calls, trace a function's
arguments, watch an address, poke a value, press buttons with no hands on the pad. Our own
tools for that were split across the PPSSPP WebSocket debugger, BizHawk Lua and one-off
`scripts/<game>-live-addresses.mjs`, and none of them run inside the app on the phone the
blind player is actually playing on.

RetroArch's MCP server (`docs/retroarch-mcp-harness.md`) covers half of it, on someone
else's cores: frame stepping, controller injection, and memory reads on Mesen, Mupen64Plus,
Beetle PSX and mGBA. It reads nothing on melonDS, PPSSPP, snes9x or Genesis Plus GX, it has
no watches, no call counts and no traces, and it is a desktop tool. This channel is the other
half, built into our own core so it works on the consoles our adapters actually run.

## Where it lives

- `Core/debug_channel.h` — the channel itself. Pure: it parses commands, schedules them by
  frame and reports, and reaches the console only through five function pointers
  (`oga::DebugHost`: read, write, set_button, exec_hook, out). A NULL pointer is a refusal
  the channel says in words ("poke refused: this console has no write path"), never a silent
  no-op. Compiled inside `Core/pokecore.cpp` (stb style, `OGA_DEBUG_CHANNEL_IMPLEMENTATION`),
  so no build's source list had to change.
- `Core/pokecore.cpp` — the host. Reads go through `AdapterReadScalar`, the same path an
  adapter's reads take. Presses go through the adapter-side pad write, so a scripted press
  never reads as the player taking over an auto walk. The channel is rebuilt at every
  `poke_start` and dropped (buttons released, hooks disarmed) before the console is torn down.
- `OgaCoreOps::write` — a new trailing member, wired only into the channel, never into an
  adapter's `oga::Host`. Adapters read; they do not write RAM.
- `gba_set_debug_hook` (`Core/gba_core.cpp`) — real mGBA breakpoints for `count` and
  `trace`, reporting r0–r3. Separate from the Lua reader's exec hooks, which keep working.

## How to use it

Save a `cmd.txt` into the channel's folder while a game runs. The core reads it about four
times a second, deletes it, and runs it. Replies are appended to `replies.log` in the same
folder and go to the app's reading log with a `DBG` prefix.

- **iOS:** `Documents/oga-debug/`. The Reading log screen also has a command field that runs
  the same commands without a file.
- **Android:** `Android/data/<package>/files/oga-debug/` (reachable over USB or adb), for the
  DS, NES, SNES and Genesis sessions.

A file is a list of lines, run in order. `wait` and `pad` hold the lines after them, so a
file can walk a menu:

```
# open the menu, go down twice, confirm, then read the cursor
pad start
wait 20
pad down
pad down
pad a 4
peek 0x0202A6C4 1
```

| Command | What it does |
| --- | --- |
| `peek ADDR [WIDTH] [COUNT]` | Read 1, 2 or 4 bytes; with COUNT, a hex dump |
| `poke ADDR VALUE [WIDTH]` | Write, then read back |
| `watch ADDR [WIDTH]` / `unwatch ADDR\|all` | Report every change with its frame; goes quiet after 30 busy frames in a row |
| `pad BUTTONS [FRAMES\|MSms]` | Hold `a+b`, `down`, `start`, `cross`…; default 6 frames, 600 at most |
| `wait FRAMES\|MSms` | Hold the lines after it |
| `count ADDR…` / `count dump` / `count reset` / `uncount` | Count calls of the code at ADDR |
| `trace ADDR [N]` | Report the next N calls with their first four arguments |
| `status`, `clear`, `help` | Where things stand; drop everything; the list |
| `speak on\|off` | Also speak replies through the announcement queue (off by default) |

Addresses are always hex (`0x`, `$` or bare). Values and counts are decimal unless written
`0x`. Lines starting with `#` are comments.

## What each console supports

| Console (host) | peek, watch, pad, wait | poke | count, trace |
| --- | --- | --- | --- |
| DS (pokecore, iOS and Android) | yes | Main RAM, shared and ARM7 WRAM | no |
| GBA (pokecore, iOS) | yes | no | yes |
| NES (pokecore) | yes | no (read-only by contract) | no |
| SNES (pokecore) | yes | yes | no |
| Genesis (pokecore) | yes | no | no |
| PSP (pokecore, iOS) | yes | yes | no |
| PlayStation (pokecore) | yes | no | no |

Game Boy and PSP on Android run their own hosts, not pokecore, so they have no channel yet.

## Defaults chosen

- Durations are frames, because that is what the console samples; `NNNms` converts at 60 fps.
- Replies are written, not spoken, unless `speak on`. A debug session should not talk over
  the game's own reader.
- The channel is always present when the app sets a folder. With no `cmd.txt` it costs one
  failed file open per poll.

## Not done

- **Watchpoints that name the writer.** `watch` sees a value change once per frame, not the
  instruction that wrote it. mGBA's watchpoints could do it for the GBA; nothing else here can.
- **`count` and `trace` on the PSP and DS.** The PSP's turn is the PPSSPP exec-hook item in
  TODO.md (the IR interpreter has a real breakpoint path). melonDS has no hook here at all.
- **Game Boy and PSP on Android**, for the host reason above.

Tests: `scripts/debug-channel-test.sh` runs `Core/debug_channel_test.cpp` against a fake
console under ASan and UBSan, then rebuilds once per `SABOTAGE_*` switch and requires each to
fail.
