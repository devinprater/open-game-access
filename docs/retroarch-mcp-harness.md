# RetroArch's MCP server as a second harness: what it can read, and what it cannot

**State: measured on 2026-10-13, on Devin's machine, against a RetroArch nightly.**
Nothing here was read and repeated; every line below came from a run whose output
is kept at `docs/evidence/retroarch-mcp-core-sweep.txt`.

## What it is

RetroArch now ships a Model Context Protocol server (`network/mcp_server.c` on
master). Its tools *are* the rows of RetroArch's own command table — the same rows
`HELP` prints — so one table now drives three interfaces: UDP, stdin, and MCP over
Streamable HTTP. The commands that matter to us:

| Tool | What it does |
|---|---|
| `SET_INPUT` / `RELEASE_INPUT` | Holds a RetroPad button or stick axis until changed. The `pad BUTTONS MS` of our workbench item. |
| `RUN_FRAMES <n>` | Advances exactly n frontend frames and stays paused. Answered when the last has run. |
| `GET_STATUS` | `PLAYING` / `PAUSED` / `CONTENTLESS`, with the core and content name. |
| `GET_MEMORY_MAP` | The core's declared memory regions: start, length, select mask, disconnect mask, kind, access, byte order. |
| `READ_CORE_MEMORY` / `WRITE_CORE_MEMORY` | Read or write core memory at a system address. |
| `GET_LOG`, `GET_CORE_OPTION`, `LIST_CORES`, `LOAD_CONTENT`, `FRAMEADVANCE`, `FAST_FORWARD_HOLD` | As named. |

⭐ **The architectural point is the same one this project already made.** RetroArch
lists one command table and serves it over three transports. We have the same
shape in `Core/oga_core.h` (`OgaCoreOps`) and `Core/systems.cpp`. An agent-facing
transport over our own ops table would be a thin layer, not a new subsystem.

## The measured result: which cores an agent can actually read

Eight cores, each loaded, then asked for its memory map. "Read" was then confirmed
by pulling 16 bytes out of a real address.

| Platform | Core | Loads | Memory map | Verified read |
|---|---|---|---|---|
| NES | `mesen` | ✅ | ✅ 32 regions (0x000–0x1f00, mirrored) | ✅ |
| N64 | `mupen64plus_next` | ✅ | ✅ RDRAM at 0x80000000 (8 MiB) | ✅ non-zero bytes |
| PS1 | `mednafen_psx_hw` (Beetle PSX HW) | ✅ | ✅ main RAM 0x000000 (2 MiB), scratchpad 0x1f800000 | ✅ |
| GBA | `mgba` | ✅ | ✅ 11 regions (IWRAM 0x03000000, EWRAM 0x02000000, …) | ✅ non-zero bytes |
| SNES | `snes9x` | ✅ | ⛔ none | — |
| Genesis | `genesis_plus_gx` | ✅ | ⛔ none | — |
| DS | `melonds` | ✅ | ⛔ none | — |
| PSP | `ppsspp` | ✅ | ⛔ none | — |
| Dreamcast | `flycast` | ✅ | ⛔ none | — |

⛔ **Every core plays. Only four can be read.** The five that cannot are not
broken — they simply never call `SET_MEMORY_MAPS`, so the frontend has nothing to
resolve an address against. `READ_CORE_MEMORY` on them answers
`no memory map defined`, and `GET_MEMORY_MAP` answers
`the running core has no memory map`. This is the same fact already recorded for
`ppsspp_libretro` (`memory_descriptors = "false"`) in
`docs/reverse-engineering/dissidia-final-fantasy.md` §98, now known to extend to
`melonds`, `snes9x`, `genesis_plus_gx` and `flycast`.

⭐ **DS booted with no BIOS present.** The melonDS libretro core played Advance
Wars – Dual Strike from a bare content path, so its read failure is not a
BIOS or setup problem.

## What this does and does not do for our adapters

**It cannot move any existing adapter onto this route.**

- The DS adapters (`fe_adapter`, `dbz_adapter`, `dq9_adapter`, the Lua readers)
  run on the vendored melonDS, and `melonds_libretro` cannot be read. Nothing to gain.
- The PSP adapters (`dissidia_adapter`, `dbzar_adapter`) run on our own PPSSPP
  integration for exactly this reason, and `ppsspp_libretro` still cannot be read.
- Neither gains anything from an outside read channel that answers nothing.

**It does add a real outside-read channel for three platforms we have no reader
for, plus one we do.**

- **N64** (`mupen64plus_next`) — RDRAM readable at 0x80000000. `n64_adapter` is the
  one to compare against.
- **PS1** (`mednafen_psx_hw`) — main RAM readable. We have no PS1 reader at all.
- **NES** (`mesen`) — readable, and we have NES readers (`nes_adapter`,
  `docs/nes-readers.md`) built on a different Mesen integration. This is the first
  outside cross-check for those addresses: the same game, a second implementation,
  one query per address.
- **GBA** (`mgba`) — readable, and the `gba-lua` readers are the other side.

**The driving half works everywhere, including DS and PSP.** `SET_INPUT` and
`RUN_FRAMES` are frontend-level; they do not need a memory map. So a menu can be
walked with no hands on the pad on *any* platform this harness can load — which is
the specific thing the open workbench item asks for, and the half that needs no
cooperation from the core.

⛔ **Careful about what a green run proves.** Driving a game through
RetroArch + MCP runs a *different* emulator instance from the one inside OGA. It
proves things about the game and about the addresses, never about our adapter
code. It is a second opinion, not a test of ours.

## Four traps, each of which cost a run

1. ⛔ **The default port cannot be used on this machine.** RetroArch's default MCP
   port is 55357. Windows has `55357–55456` reserved here (Hyper-V/WSL), so the
   server logs `[MCP] Could not listen on 127.0.0.1:55357` and never listens. The
   span 52900–55356 is free. Check the machine with
   `netsh int ipv4 show excludedportrange protocol=tcp`.
2. ⛔ **`pause_nonactive` defaults to `true`, and a scripted RetroArch never has
   focus.** The content therefore starts `PAUSED`, and `RUN_FRAMES` never completes
   — it times out at *every* frame count, down to one, which reads exactly like a
   broken tool. Set `pause_nonactive = "false"` and it reports `PLAYING` and runs.
3. ⛔ **Feed it a minimal config; do not reuse the config RetroArch writes back.**
   RetroArch rewrites its config on exit and writes `libretro_directory = ":\cores"`
   — a mangled path. An instance started from that file came up contentless and
   failed every `LOAD_CONTENT`; the same commands against an instance started from
   a five-line config worked. Keep the hand-written config and let RetroArch expand
   its own copy elsewhere.
4. ⛔ **Paths in the JSON must be native Windows paths.** `C:/…` works. An MSYS
   `/c/…` path makes `LOAD_CONTENT` answer `ERROR Failed to load content.` — and, on
   this build, it *unloads the running game first*, leaving `CONTENTLESS`. This is
   the third time this project has paid for MSYS path conversion, after `git -C`,
   `rg` and RetroArch's own `-L`.

## The upstream pull request, and the bug it fixes

`libretro/RetroArch#19815` (draft, unmerged) makes `LOAD_CONTENT` check both the
core path and the content path *before* it unloads, name the path that failed, and
attach structured rows to replies. The nightly tested here still has the old
behaviour, and we reproduced it exactly:

```
BEFORE   GET_STATUS                  -> PAUSED Snes9x,Chrono Trigger (USA)
LOAD_CONTENT with a bad core path    -> LOAD_CONTENT ERROR Failed to load content.
AFTER    GET_STATUS                  -> CONTENTLESS
```

One wrong path destroyed a running game and never said which path was wrong. For
us this is the whole argument for the fix: a scripted run cannot see a screen, so a
command that throws the run away and then explains nothing is the worst available
shape. Two asymmetries worth recording — an *empty* content path was rejected
*without* unloading, and only a bad *core* path destroyed the run.

⛔ **`structuredContent` is absent from this build**, which confirms the request is
not merged. Do not write a client against it yet.

## A defect on the second transport

The `--mcp-stdio` transport answers `initialize`, `ping` and `tools/call`
normally, but **`tools/list` answers nothing and leaves the connection wedged** —
every later `tools/call` on that session also goes unanswered, though the process
stays alive. Over HTTP the same call returns 200 with 23,277 bytes and 80 tools.
Since asking for the tool list first is what every MCP client does, stdio is not
usable as it stands. **Use HTTP.**

Also measured: the server gives a deferred command **ten seconds** to finish, so
`RUN_FRAMES 300` succeeds and `RUN_FRAMES 900` answers `The command did not answer
in time.` Long stretches must be sent in chunks of roughly 600 frames.

## Running it

The application is not in the buildbot's `RetroArch.7z` — that is the assets
package only (16,539 files, no executable). The app is in
`<date>_RetroArch-Win64-setup.exe`, and 7-Zip unpacks that installer directly with
no install.

```ini
# a small hand-written config; see trap 3 for why not to reuse the saved one
mcp_server_enable = "true"
mcp_server_port = "54123"          # trap 1: not 55357 on this machine
mcp_server_bind_address = "127.0.0.1"
mcp_server_token = "…"             # the server does not start without one
pause_nonactive = "false"          # trap 2
```

```
retroarch.exe -c ra-run.cfg -L <native path to core dll> "<native path to content>"
# then POST JSON-RPC to http://127.0.0.1:54123/mcp
#   Content-Type: application/json
#   Accept: application/json, text/event-stream
#   Authorization: Bearer <token>
```

A working sweep script is kept beside this note's evidence:
`docs/evidence/retroarch-mcp-core-sweep.txt` is its output, and the script itself
is on Devin's machine at
`C:\Users\Devin Prater\Downloads\ra-mcp-test\sweep.sh`.

## Where this sits against the workbench item

The open item (see `TODO.md`) wants one channel a player can drive *while playing*,
with controller injection and hardware-level reads. This is a measured, working
half of it — injection, exact stepping, liveness and, on four cores, genuine
address reads — reached for the price of one binary download and no code of ours.
What it is not is a replacement for our own adapters: it reads other people's
cores, on consoles where those cores cooperate, and it can never see our code.

⭐ **The next experiment, if it is worth one:** point a Mesen session at the same
NES game our `nes_adapter` reads and compare addresses one by one. That is the
first time a second implementation can be asked the same question.
