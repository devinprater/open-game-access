# Dragon Quest IX: Sentinels of the Starry Skies (US, YDQE)

Reader: [RetroSanity/DQ9-Access](https://github.com/RetroSanity/DQ9-Access),
tracked as a submodule at `third-party/DQ9-Access` so `git submodule update
--remote` picks up new releases. Note the repo ships the mod as
`DQ9 Access Mod.zip` (script + collision map + README); the sources live
inside the zip, so after updating, re-extract and re-read the header comment
— the memory map is documented there, not in a separate file.

Target: Nintendo DS, US version (ROM game code `YDQE`). A BizHawk/EmuHawk
(melonDS core) Lua script, `dq9-access.lua` (~4,200 lines) plus a 2.5 MB
pre-baked collision map (`dq9-access-maps.bin`). Needs a speech-enabled
EmuHawk build (stock BizHawk has no `speech.say`).

## What it teaches that OGA didn't already do

1. **Read the finished words, not the widgets.** DQ9 builds every string as
   plain ASCII with small `<TAG>` codes before drawing. The mod reads the
   message box (`0x0211819C`, pages split by bytes `0D FF`) and the menu
   markup buffer (`0x02118FFC`, e.g.
   `<CURSOR=1><N=0>Fight</N>\n<N=1>Examine</N>…`) straight out of RAM.
   No UI-struct walk, no OCR. Where a game centralises its text like this,
   one buffer beats a dozen struct hunts.
2. **Ship the walls with the mod.** Routes and auto-walk run over collision
   data extracted from the game (`dq9-access-maps.bin`), not live collision
   reads: plan around walls, climb stairs, stop in front of doors, approach
   counters from the front and signs from the readable side. v0.2 added
   counter/sign facing — the detail that makes "walk me there" actually
   arrive usable.
3. **Camera-relative directions.** The mod converts the camera position
   (`0x0210A134`) into D-pad words, so "up" means push up even when the
   camera sits at an angle. Any 3D game with a rotating camera needs this
   layer between map geometry and spoken directions.
4. **A nearby list beats a radar.** People, monsters, items, sparkles,
   events, doors, exits, marks — one list, nearest first, with hidden
   things included (e.g. the item the dog finds). Browsing it (E-list keys
   in v0.1, PageUp/PageDown + Ctrl categories in v0.2) plus "directions to
   this" plus "walk me there" is the whole travel loop.
5. **Persistence is a feature.** Marks, learned place names and opened
   chests live in `dq9-access-places.txt` across sessions. OGA adapters
   should plan for a per-game sidecar from the start, not as an afterthought.
6. **Snapshot key for bug reports.** `C` writes `dq9-access-log.txt`; silent
   spots arrive as data, not vibes. Worth copying everywhere.

## v0.2 (30 Sep 2026) — what changed since v0.1

Reads: spell/ability MP costs (plus caster's remaining MP in battle), ally
HP when choosing a target, shop "how many?" amount + total (plus who holds
how many), per-person equipment deltas while browsing a shop, and the post-
purchase "Who?" list with per-person deltas. Travel: counter and sign/statue
routing. Keys: nearby-list paging (PgUp/PgDn, Ctrl for categories), route
(Ctrl+Home) / auto-walk (Ctrl+End) on the picked thing, H/M party info;
Stop moved to tap-Ctrl, Attributes to L, Equipment to U, mark to Shift+M;
game keys mapped for keyboard play. Fixes: dead members no longer announced
for turns, no phantom prices in the Who list, versioned startup line.

## Native adapter status (`Core/dq9_adapter.cpp`, game code `YDQE`)

`WhereAmI` (map code + tile position + battle/exploring), party cycling
(`NextAlly`/`PrevAlly`/`NextUnactedAlly`), nearby-object scan on
`NextEnemy`/`PrevEnemy` with camera-relative directions (object table
`0x02107600`/`0x02107680`, camera `0x0210A134`, nearest-first, generic
labels — someone / something / story character; learned names stay mod
territory), menu-cursor echo on `MenuState` (`<CURSOR=n>` + `<N=i>` items,
battle lists trusted only at phase 3, yes/no prompts named without guessing
the cursor), `DumpState`. Ready-gated on map code AND printable slot-0 name;
either alone lies (see below).

Live-checked Oct 1 2026 against a title snapshot (`fe/plans/dq9-title.txt`
boots the US ROM headless, `dq9-t0.ram`): slot 0 holds printable junk
("NineRZ") with no map, the battle flag reads SET, menu/message buffers are
empty — the gates refuse all of it (not-ready / no-menu / unknown, nothing
spoken). In-game speech still needs scripted play past name entry.

Deliberately NOT claimed: the on-screen map NAME at `0x022A4266`
sits above the Host's 4 MiB main-RAM window, so `WhereAmI` reports the map
code (`0x020FB3FC`, e.g. `M01M0100`) instead of guessing a name. Dialogue
queueing, shop/skill flows, route planning + auto-walk, and place marks stay
mod (Lua) territory — porting that state machine would fork it into a copy
that drifts.
