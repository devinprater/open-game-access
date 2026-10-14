# Where the Chrono Trigger answers live (paths to keep)

Written 2026-10-14, after probes 11-42, so a later session does not have to rediscover them.
Everything below is in this repository unless it says otherwise. Two of these files did more for
the adapter in one afternoon than a week of sweeping: the recorded recipes and the location-name
table.

## The five vendored projects, and what each is for (`third-party/`)

| project | what it is | reach for it when |
|---|---|---|
| **`belthasar/`** | A native C re-implementation of Chrono Trigger (SNES, USA, 1995), checked frame by frame against the original | The reference implementation. Its notes are the contract: when the ROM does something surprising, this says why |
| `chronoret/` | ChronoRET, a matching decompilation (65816 assembly) | Reading a routine as source rather than as disassembly |
| `ct-recomp/` | ct-recomp, a recompilation project | A second opinion on a routine, and its own roadmap |
| `ct_disassembly/` | Raw disassembly of CT (US 1.0), one file per ROM bank (`bank_C0.asm`, `bank_C1.asm`, `bank_C2.asm`, …) | A bank and address to read, with no build needed |
| `ctviewer/` | CT Viewer: displays scenes and world maps with debug information | Its data folder, and looking at a map without an emulator |

## belthasar: the parts that have earned their keep

- **`tools/recipes/*.json` — 105 recorded input recipes.** Each names a parent recipe, the steps
  to run (pad scripts, pokes, teleports), and an **asserted result** (`loc`, WRAM values). These
  are savestates that could be published: they rebuild the state instead of shipping it.
  **They are directly replayable through the MCP harness** — verified on `fair_entrance.json`,
  which landed on loc `005` with gold `0x190`, exactly its `expect` block.
  The ones that map onto the adapter's walk through the game:
  `bedroom`, `home_allowance`, `home_downstairs`, `ow_nav_w0`, `ow_nav_downstairs`, `ow_w0`,
  `ow_intro_pan`, `fair_entrance`, `fair_candy`, `fair_pendant`, `marle_joins`, `lucca_joins`,
  `frog_joins`, `name_crono`, `name_lucca`, `name_mom_box`, `name_home_stairs`, `gate_to_600`,
  `cathedral_entry`, `cathedral_hall`, `castle_600`, `forest_entry`, `truce_market`, `gato_front`,
  `menu_shop`, `menu_equip_spare`, `telepod_show`, `queen_vanishes`, `fxf_scene13_stairs`,
  `fxf_s49_chest_walk`. The `fx_*`/`fxf_*`/`fxm_*` families are fixtures built from other recipes.
- **`tools/recipes.py`** — the recipe format itself: steps (`power_on`, `boot_to_bedroom`, `pad`,
  `poke`, `teleport`, `sram`), the **pad syntax** (`mask:frames,...`, `0:N` waits, groups with
  `*N`), and the assertion. The pad bits are LakeSnes serial order: `R=80 L=40 D=20 U=10 Start=08
  Select=04 Y=02 B=01, A=100 X=200, L=400 R=800` — `B` is the run button.
- **`src/collision.h`, `src/collision.c`** — the byte-exact collision model: the three property
  planes, the shape decode, the z-gate, actor collision, and the feeler path.
- **`src/field.c`** — the field engine port. Its comments carry the WRAM layout of the planes and
  the load/stream behaviour that no amount of probing produced cheaply.
- **`src/ct_vm.h`, `src/ct_vm.c`** — the field VM's opcodes (the `E3 00`/`E3 01` control lock among
  them) and its state.
- **`src/ct_world.c`, `src/ct_owobj.h`** — the overworld module, i.e. the part the field's
  addresses do not describe.
- **`src/harness_drive.c`** — their own drive harness (`--btnscript`, `--teleport`, `--peek`): the
  same job this project's MCP harness does, worth comparing against.
- **`disasm/notes/` — 267 per-topic dossiers**, plus **`disasm/notes/_collision_specs/*.json`**,
  verified pseudocode per routine. Those that have earned their keep here: `collision.md`,
  `field_exit_scan_guard.md`, `field_door_list.md`, `field_tick.md`, `menu2_overworld_entry.md`
  (the overworld is its own module), `field_load_fade_in.md`, `field_dash_run.md`,
  `field_caption_plane.md`, `field_item_gold_family.md`.
- **`docs/`** — `ARCHITECTURE.md`, `METHODOLOGY.md`, `DECISIONS.md`, `ROM.md`, `TESTING.md`,
  `CAPTURES_WANTED.md`, `ROADMAP.md`, `DEBUG_SURFACE.md`.
- **`snes/`** — the vendored LakeSnes core that serves as their oracle; `snes/input.c` holds the
  serial bit order the pad scripts are written in.
- **`tests/test_host_tools.py`** — their host-side tests.

## The ROM's own name table

- **`ctviewer/data/snes_na_scenes.txt`** — every location, in order, keyed by the `$7E0100` byte
  the adapter already reads. `001` Crono's Kitchen, `002` Crono's Room, `003` Lucca's Kitchen,
  `004` Lucca's Workshop, `005` Millennial Fair, `006` Gato's Exhibit, `007` Prehistoric Exhibit,
  `008` Telepod Exhibit, `00C` Truce Inn (Present), `00D` Truce Mayor's Manor 1F, `00E` Truce
  Mayor's Manor 2F, `011` Truce Market (Present), `013` Guardia Forest (Present), and `1F0` for
  the 1000 A.D. overworld. This is the narration table the adapter wants, and it is the ROM's own.

## OGA's own Chrono Trigger writing

- **`reverse-engineering/ctds/snes-verify.md`** — the live-verification log: probes 1-42, every
  address checked against the running game, with the retired claims marked as retired.
- `reverse-engineering/ctds/prior-art.md`, `ram-leads.md`, `tooling-leads.md` — the earlier surveys.
- `docs/evidence/ct-collision-planes-and-the-stairs.txt`, `ct-walk-and-compendium.txt`,
  `ct-snes-mcp-probe.txt` — raw run output behind the probes.
- `docs/research/chrono-trigger-decomp-and-recomp.md`, `chrono-compendium-findings.md` — the
  project survey and the community's live RAM map, checked entry by entry.

## How to use all of it

1. **Before probing anything, grep these.** A mechanism asked about here usually has a note, a
   recipe or a line of C already: the recipe-plus-name-table pair answers "where am I, how do I
   get there and what is it called" in minutes.
2. **To replay a recipe through MCP:** parse `mask:frames`, map each bit to a `SET_INPUT` control
   (`B` = run), hold and release in order, honour the `0:N` waits, then check the `expect` block
   against `$7E0100` and the WRAM values. Two implementations agreeing is worth more than one.
3. **When a read looks like a freeze, look at the screen.** `SAVE_STATE_SLOT` writes a `.png` next
   to the state; a screenshot has settled arguments that three probes of byte-reading got wrong.
4. **Mind which screen owns the address you are reading.** The field's flags, latches and player
   position do not describe the overworld, and the overworld does not clear them.

⚠ Not in the repository, and not reproducible from it alone: the harness itself, at
`C:\Users\Devin Prater\Downloads\ra-mcp-test\` on Devin's machine — the `ct_probe*.py` scripts, the
`snes9x2010` core, and the save states (slot 2 the bedroom handover, slot 13 the overworld outside
the house, slot 15 the Millennial Fair). The ROM is `Dropbox/games/SNES/Chrono Trigger (USA).sfc`.
