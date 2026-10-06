# DECOMP_INDEX.md — Dissidia Final Fantasy (PSP, ULUS10437 v1.00)

Address → meaning map for the Ghidra decompilation. **Search this first** (workspace
directive, step 2) before touching RAM or the ISO. Entries are grouped by system; every
`size`/`callers` figure is measured from the Ghidra project (not remembered), and every
meaning is sourced from the notes cited at the end.

> **Placeholder resolved.** The workspace directive referenced `<DECOMP_PATH>` and
> `<ISO_PATH>` literals. Neither exists as a path on disk. The real artifacts are listed in
> §1 below — that is what those placeholders mean.

---

## 1. Paths and entry points (the real `<DECOMP_PATH>` / `<ISO_PATH>`)

| what | path |
|---|---|
| **PRIMARY decomp workspace** | `C:\Users\Devin Prater\oga-ghidra-dissidia\` (Ghidra project + Java scripts + reports) |
| Ghidra **project to use** | `DISSIDIA_ELF` (imported as an **ELF**; see §2 — the `DISSIDIA` project is skewed and superseded) |
| decrypted executable | `...\oga-ghidra-dissidia\EBOOT.dec` (4,731,180 B, MIPS R3000 LE, sha256 `a42328831ebba713d3eacabc51cd3f49cc07669ce684df0a823ee3e0c393243a`) |
| machine-readable function dump | `...\oga-ghidra-dissidia\function-index.txt` (15,368 functions: `entry  size  callers  name`) |
| **runtime verification only** (do NOT open for logic questions) | `\\wsl$\...\home\devin\dissidia.cso` (1,370,502,434 B) |
| notes / scripts / findings | `~/oga-work/docs/reverse-engineering/` (WSL) |

⛔ **Load base is `0x08804000`** (ULUS10437 **v1.00 only**). RAM address = `0x08804000 + ELF vaddr`.
Never carry this base to another build or game.

⛔ **Readable user RAM is exactly `0x08800000`–`0x0A000000` (24 MiB).** A `memory.read` that
crosses `0x0A000000` **fails whole (returns 0 bytes)** rather than truncating — probe the
mapped span first, never chunk past the end.

---

## 2. How to query the decomp (the pipeline, verified)

**Use the `DISSIDIA_ELF` project, never `DISSIDIA`.**

- `DISSIDIA` was imported with `BinaryLoader -loader-baseAddr 0x08804000`. Its first LOAD
  segment has file_off `0x74` but vaddr `0`, so every file offset N maps to `base+N` while
  MIPS code references `base+(N-0x74)`. **That 0x74 skew made a whole string-reference scan
  report 0 matches across 683,649 instructions.** See §6 for the two reports that carry the
  wrong null.
- `DISSIDIA_ELF` was imported with `-loader ElfLoader` so Ghidra applies the segment mapping
  itself: **addresses in the listing ARE the addresses the code uses.**

**Search the listing for the string BYTES — never precompute a string's address and ask for
references.** Precomputing in the wrong address space returns a silent, convincing 0
(this has failed three separate ways on this project). The working shape:

```java
Memory mem = currentProgram.getMemory();
Address cur = block.getStart();
while (cur != null && cur.compareTo(block.getEnd()) < 0) {
    Address found = mem.findBytes(cur, block.getEnd(), needle, null, true, monitor);
    if (found == null) break;
    hits.add(found);
    cur = found.add(1);
}
// then getReferencesTo(found), decompile each referrer
```

`Memory.findBytes(...)` is on the **`Memory` interface** — calling it on a `MemoryBlock` is a
compile error.

**Run headless scripts through a `.bat` wrapper** (bash/MSYS splits the space in the user dir):
`run-analysis.bat`, `run-import-elf.bat`, `run-index.bat`, and the per-report `run-*.bat` files
all live in the project dir. `analyzeHeadless ... -process EBOOT.dec -noanalysis` reuses the
saved project and starts in seconds — **do not re-import**.

⛔ Ghidra scripts must be **Java**, not Python: PyGhidra 3.1.0 pins `jpype1==1.5.2`, which has
no `cp314` wheel, so `analyzeHeadless -postScript x.py` fails on this machine's Python 3.14.
⛔ A `ClassNotFoundException` from a script **IS** a compile error — grep the output for
`error:` / `location:` / `skipping`, or compile separately first.

---

## 3. Function index — by system

Format: `FUN_addr  size  callers  meaning`. Sizes/callers measured from
`function-index.txt`; meanings from the notes. **Bold** = named/confirmed identity.

### 3.1 Boot / engine init

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_002dbf50` | 1192 | 1 | **the whole boot sequence**: 2 MiB work buffer, localisation loads, ordered subsystem constructors |
| `FUN_000e5710` | 592 | 141 | general heap allocator |
| `FUN_00103aac` | 24 | 1 | 2 MiB work-buffer alloc |
| `FUN_000e8bc8` | 188 | 44 | file-load primitive (`general_archive/main/main.bin`, `…/EN/main_lang.bin`) |
| `FUN_000e8dc0` | 112 | 46 | loader variant (called with `PTR_s_general_archive_main_main_bin_0039b300`) |
| `FUN_000fa84c` | 6856 | 1 | **global subsystem initialiser** (9 refs, sequential) |
| `FUN_0033c468` | 76 | 429 | tail routine reached by every `param_2 & 1` test |
| `FUN_0033c4d0` | — | — | static-init / registration helper (called with `DAT_` pointers) |
| `FUN_003406c4` | 52 | 210 | array initialiser (used by the menu-manager constructor) |
| `FUN_00354d84` | 24 | 70 | packs values into caller buffers (input path) |
| `FUN_00354db0` | 40 | 262 | no-return packer into caller buffers |

### 3.2 Resource loaders / archives

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_00122ccc` | 6004 | 1 | **archive path builder** (`general_archive`, `general_archive/field/JP/menu_lang.bin` template) |
| `FUN_001224c4` | 684 | 1 | called by the path builder at `0x00123EE8` |
| `FUN_00124970` | 1640 | 1 | **menu resource loader** (`pause_help.bin`, `ap_bonus.bin`, `dp_bonus.bin`, `info.bin`, `command_battle.bin`, `result.bin`, `judgement.bin`, `super_skill.bin`, `replay*.bin`, `org99.gmo`, `battle_dialogue.sequence`, …) |
| `FUN_001ea728` | 264 | 1 | `accessory_help.bin` loader |
| `FUN_001f4df4` | 272 | 1 | `item_help.bin` loader |
| `FUN_001f06cc` | 3400 | 1 | `system.bin` loader |
| `FUN_00104bb8` | 292 | 1 | `VOLATILE_MEMORY_LOADER` |
| `FUN_000fc7dc` | 204 | 3 | **XOR decoder** (XORs a buffer against bytes at `&DAT_00392be2`) |

### 3.3 Text / localisation

- Menu **labels are textures, not strings** (on disc *and* in RAM) — do not hunt them as text.
- The decoded UI text lives in RAM as **UTF-16LE with a 1-byte format prefix per entry**;
  system/save strings coexist as **ASCII**. The on-disc `*_help.bin` / `friend_card.bin` /
  `name.bin` are encoded and were never decoded — **read the decoded form out of RAM instead**.
- The localisation file is **`general_archive/main/EN/main_lang.bin`** substituted into the
  `/JP/` template at runtime — and it is **not present on the disc image** (expected; the path
  is composed, so searching the ISO for the final name is wasted work).
- RAM text regions (measured): `0x09D16xxx` pause-menu labels · `0x09E6Axxx` ability names ·
  `0x09E58xxx` save/autosave messages · `0x08B7xxxx` engine/manager names.

### 3.4 Input / pad layer

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_000f7598` | 164 | 1 | **pad constructor** (`FUN_000f7598(&DAT_0132fb40)`) |
| `FUN_000f7994` | 152 | 1 | **sceCtrl sampling setup** (`FUN_0036da44(1)`, `FUN_0036da54(0x411b)`); registers `FUN_000f790c` |
| `FUN_000f790c` | 136 | 1 | registered via `FUN_00103db0` (the same mechanism the menu manager uses) |
| `FUN_000f68e0` | 548 | 2 | **controller read** (sceCtrl via `FUN_0036da4c`) |
| `FUN_0036da4c` | 8 | 2 | `sceCtrl` read |
| `FUN_0036da44` | 8 | 1 | `sceCtrl` sampling on |
| `FUN_0036da54` | 8 | 1 | `sceCtrl` sampling setup (`0x411b`) |
| `FUN_000f76f4` | 152 | 1 | pad subobject update (**the one function doing real bit manipulation**, 5 genuine bit tests) |
| `FUN_000f778c` | 100 | 1 | pad subobject update |
| `FUN_000f77f0` | 100 | 1 | pad subobject update |
| `FUN_000f6694` | 232 | 4 | **converted-input sink** (the translated bit pair is passed as arguments) |
| `FUN_000f6fc8` | 84 | 1 | breakpoint target to capture the converted-input object (`param_1`) |
| `FUN_000f67c0` | 28 | 1 | reads converted-input |
| `FUN_000f6604` | 132 | 6 | table walk with two sentinels (`0xfffffffe`) |
| `FUN_000f71c8` | 92 | 0 | **function-pointer holder target** (`0x08BA46F4 -> FUN_000f71c8`); the guarded query |
| `FUN_000f7224/7280/72dc/7338` | — | — | the pointer table that follows `0x08BA46F4` |
| `FUN_000f7420` | 32 | 118 | **junction of the menu and input layers** |
| `FUN_000f8808` | 92 | 1 | junction of the menu and input layers |
| `FUN_000f7498` | 88 | 10 | drives **both** converted-input ports |
| `FUN_000f7028` | 156 | 2 | computes nothing itself — read the callee when the caller passes values |
| `FUN_000f70c4` | 116 | 1 | callee of `FUN_000f7138` (two calls at `entry+0x18`) |
| `FUN_000f7138` | 112 | 2 | contains two `FUN_000f70c4` calls |
| `FUN_000f5e0c` | 88 | 0 | installs vtable pointer `*(param_1+0x74) = &DAT_003a06a0` |
| `FUN_000f5e80` | 52 | 1 | called by `FUN_000f5e0c` |
| `FUN_000f67dc/67f8/6814/6830` | 28/28/28/56 | 1 | small guarded pad accessors |

### 3.5 Menu manager & selection — **UNRESOLVED; do not re-tread the dead ends**

The menu's logical **selection index was never found**. Four separate "static anchor" candidates
were each explained away, and the index is reached through a runtime pointer the code builds at
menu-open time. Read §7 before spending time here.

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_002489d0` | 1024 | 1 | **MENU MANAGER constructor** (`"MENU MANAGER"`); builds FIVE parallel subsystems; allocates a `0x40c` object via `FUN_00247b40` stored at `param_1[1]`; registers 3 callbacks |
| `FUN_00247b40` | 56 | 1 | allocates that `0x40c`-byte menu-manager state object |
| `FUN_0024932c` | 380 | 1 | **event dispatcher** — walks TWO item linked lists (`head +0x28`/`+0x34`, `next +0x24`, `flags +0x14`, counter `+0x17`) |
| `FUN_0024aee0` | 4776 | 2 | large menu **renderer/draw** (NOT a menu index — size tells you) |
| `FUN_0025595c` | 2108 | 2 | menu update path |
| `FUN_0025468c` | 132 | 1 | definition lookup `base + index*0x1c` |
| `FUN_0024910c` | — | — | list maintenance, free pool at `+0x1490`/`+0x1494` (nodes are **recycled** → no stable node address) |
| `FUN_00251278/0025236c/00252424` | 56/56/128 | 6/6/3 | selection getter / field-role candidates (roles unconfirmed) |
| `FUN_002500f4` | 8 | 11 | accessor |
| `FUN_00251238` | 8 | 8 | accessor |
| `FUN_00251da4` | 8 | 8 | accessor |
| `FUN_00251de4` | 56 | 2 | **"return the selected item"** |
| `FUN_0025051c` | 28 | 5 | returns an action enum (0 none / 1 confirm / 2 cancel) from `[W+0x28]`+`[W+0x38]` |
| `FUN_00250538` | 148 | 5 | callee of `FUN_00267cb8` |
| `FUN_002478e0` | 144 | 1 | draw-call receiver (`manager+4, node+0x10, index`) |
| `FUN_00248dd0/00248dec/00248ea8` | 28/188/188 | 2/2/1 | the three handlers' one-call **thunk targets** (keep following until the body works) |
| `FUN_0036926c/00369290/003692b4` | 36/36/36 | 0 | the three **registered per-frame callbacks** (vtable-dispatched → no findable callers) |
| `FUN_00267cb8` | 540 | 0 | **menu-side consumer of the input API** (vtable-dispatched) |
| `FUN_00267740` | 256 | 1 | branch of `FUN_00267cb8`: builds the `0x3B`/`0x3A` two-item list |
| `FUN_00267840` | 148 | 1 | other branch |
| `FUN_00103c60` | 8 | 21 | **8-byte engine-handle accessor** `return *(param_1 + 0x2000)` (28 call sites) — not a container |
| `FUN_00103db0` | 152 | 52 | the **event/callback registration mechanism** |
| `FUN_0012ebcc` | 540 | 2 | fixed front-end selection handler (companion builder `FUN_0012fcd4`) |
| `FUN_001256f4` | 592 | 1 | small modal/branch-result handler (accepts item values `0x0C`, `0x17`) |
| `FUN_0012961c` | 1128 | 1 | broad front-end/menu dispatcher (tags `8–0x19`, `0x3F–0x45`) |

### 3.6 Board / world map — **resolved and adapter-wired**

Holder chain: `M = [0x08B98940] -> P = [M+0x118] -> B -> T/D`. Node array `[T+4]`, count `u16 *[T]`,
stride `0x10`, coordinates `+2/+3`, catalog key `+0`, flags `+0x0C`; cursor stored at
`D+0x194/+0x195`; origin at `D+0x38[+2/+3]`.

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_001c218c` | 688 | 0 | **board tick** (calls `FUN_001c418c([P+4])`) |
| `FUN_001c418c` | 8 | 22 | returns `[B+0x10]` |
| `FUN_001c4184` | 8 | 18 | accessor |
| `FUN_001c417c` | 8 | 11 | accessor |
| `FUN_001c4194` | 28 | 1 | allocates bundle `B` (`0x18`) |
| `FUN_001c3ff0` | 388 | 1 | constructs `B+8`, `B+0x0C`, `B+0x10`; calls `FUN_001b8ef4` |
| `FUN_001b8ef4` | 1104 | 1 | **board constructor** |
| `FUN_001b8728` | 520 | 1 | **available-direction primitive** — probes exactly `(ox±1,oy)`, `(ox,oy±1)` |
| `FUN_001b8930` | 484 | 1 | **eligibility filter** (four-rule) |
| `FUN_001b72e4` | 232 | 3 | movement consumer of the `width*height` flag-byte array |
| `FUN_001af3b0` | 444 | 2 | render/update consumer of the same flag array |
| `FUN_001b04fc` | 1516 | 1 | render/update family |
| `FUN_001b8e40` | 180 | 3 | returns `1`, or `0xDD`/`0x0A` if it rejects |
| `FUN_001b1d08` | 368 | 1 | type `5` board objects (unique two-part animation/setup) |
| `FUN_001b22d4` | 2788 | 2 | type `8` board objects: sets node flag `0x20` (non-rendered/disabled) |
| `FUN_001b6d04` | 368 | 6 | side effect performed during a probe |
| `FUN_001b6abc` | 404 | 1 | event arming |
| `FUN_001b6084` | 164 | 3 | **DP adjuster**: `s16[[obj+0x38]+6] += delta` (signed), clamps, syncs, refreshes cache |
| `FUN_001ca124` | 52 | 1 | DP **cache refresh** |
| `FUN_001ca7ec` | 156 | 1 | battle-side DP subtract primitive (`[0,max-1]` clamp) |
| `FUN_001d5438` | 4768 | 4 | DP/**HUD draw tick** (presentation only — never game logic) |
| `FUN_001b97c8` | 17120 | 1 | **the big battle/field UI state dispatcher** (serves the pause widget and board input) |
| `FUN_001be9a4` | 620 | 1 | allocates bundle `B` via `FUN_001c4194(0x18)` |
| `FUN_001becac` | 8 | 3 | returns byte `+0x40` (current story/tile class) |
| `FUN_001bf078` | 380 | 2 | commits the resolved id to the active board object |
| `FUN_001c4944` | 304 | 1 | parses the `rmfd` asset into three sibling objects |
| `FUN_001c4ae4` | 8 | 2 | the middle `rmfd` sibling |
| `FUN_001c4bb8` | 8 | 5 | returns `u8 [A+0]` |
| `FUN_001c4bc0` | 8 | 1 | returns `u8 [A+1]` |
| `FUN_001c5070` | 280 | 3 | initialises **32 records at `+0x11C`** (= logical `R+0x114`, the marker/state array) |
| `FUN_001c52cc` | 44 | 11 | proves the chapter stride/base |
| `FUN_001c52f8` | 60 | 13 | proves the slot stride and `+0x08` record header |
| `FUN_001c5b4c` | 76 | 1 | initialises `T+4 = R+0x114` (sparse `0x10`-byte marker array) |
| `FUN_001c5bb4` | 288 | 14 | scan — calls `FUN_001c4740(*T)`, reads nodes `[T+4] + id*0x10` |
| `FUN_001c5cd4` | 64 | 9 | `(T,i)` returns catalog key `+0x00` |
| `FUN_001c5d90` | 32 | 27 | resolves a key through `C` |
| `FUN_001c5d14` | 124 | 5 | CROSS consumer, state `0x28` highlight |
| `FUN_001c6df4` | 28 | 28 | returns `u8 [A+0]` |
| `FUN_001c6e10` | 28 | 23 | returns `u8 [A+1]` |
| `FUN_001c4828` | 68 | 1 | `O+0x30` mutable catalog/object enable byte |
| `FUN_001da298` | 13124 | 1 | **candidate confirmed-move state machine** (states `0x15–0x16`) |
| `FUN_001cad14` | 80 | 2 | called by the state machine |
| `FUN_001cad64` | 216 | 2 | called by the state machine |
| `FUN_001ae3b4` | 44 | 1 | records the chosen move |
| `FUN_001ae57c` | 8 | 4 | save-progress probe |
| `FUN_001ae5c4` | 8 | 1 | save-progress probe (`==0` sets flag) |
| `FUN_001ae5cc` | 12 | 3 | restores saved x/y |
| `FUN_001ae5e8` | 44 | 6 | persistent-story gate (`...,6`, `...,8`) |

### 3.7 Battle — fighters, objects, lock, EX

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_00097778` | 7656 | 0 | **battle event dispatcher** — `switch` on `event+2`, ~70 cases; `case 0x3C` is the **only EX-gauge writer** (clamps to 10000.0) |
| `FUN_0009c864` | 28 | 7 | allocates the `0x4ED0`-byte fighter |
| `FUN_0009eb98` | 192 | 0 | inserts common objects into `M+0x0C`/`M+0x10`, sorted by type byte `obj+0x530` |
| `FUN_000b1020` | 32 | 8 | generic-list accessor (`null -> [M+0x0C]`) |
| `FUN_000b05e0/000b0848/000b0f90` | 508/172/108 | 0/0/4 | walk `[O+0x490]` |
| `FUN_000b0954` | 344 | 1 | unlinks objects |
| `FUN_000b1040` | 32 | 27 | returns `[DAT_003915a0+0x14]` first call, `[P+0x4EA8]` after |
| `FUN_000b1060` | 56 | 2 | walks the same chain and counts it |
| `FUN_000b64f8` | 88 | 4 | **enemy-lock writer**: `*(p+0x2ec) = *(p+0x2f0)` |
| `FUN_000b6550` | 128 | 2 | **lock-off writer**: `*(p+0x2ec) = 0` |
| `FUN_000a485c` | 760 | 5 | **common-object initialiser** (XYZ at `O+0x80/+0x84/+0x88`, lock field) |
| `FUN_000b1b78` | 328 | 3 | adds an EX modifier to `S+0x14`, clamps 0..10000 |
| `FUN_000b1d60` | 84 | 1 | adds damage to `S+0x02`, records hits `+0x04/+0x06` |
| `FUN_000ba0c8` | 220 | 5 | full-EX check (`10000.0 <= [S+0x14]`) |
| `FUN_000baf9c` | 4092 | 1 | earlier wrongly attributed as the list inserter — it is not |
| `FUN_000bbf98` | 1352 | 0 | per-fighter update routine |
| `FUN_000bd3e4` | 1264 | 1 | **the actual fighter constructor/inserter** |
| `FUN_000be1f8` | 44 | 1 | derives outcome values |
| `FUN_000be2cc` | 644 | 1 | routes outcome `1/2/5`, `4`, `3` through different handlers |
| `FUN_000a1888` | 128 | 3 | forms target consumers from class byte `O+0x530` |
| `FUN_000a8aa8` | 252 | 10 | changes battle mode, invokes old/new callbacks |
| `FUN_000a0ff0` | 176 | 1 | copies level / HP-damage / EX into a presentation-save mirror |
| `FUN_000ce824` | 1424 | 1 | **EX pickup poster** — posts event `0x3C` + one of three SFX (`0x2135/0x2136/0x2137`) by level |
| `FUN_001accb4` | 60 | 28 | event-poster primitive |
| `FUN_0009b930` | 180 | 90 | SFX poster |

### 3.8 Talk events / story scenes (`.evex`)

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_001c3654` | 2040 | 1 | **talk-event manager init** |
| `FUN_001b4df0` | 216 | 1 | `.evex` loader |
| `FUN_001b5ca4` | 124 | 5 | `.evex` index loader |
| `FUN_001c488c` | 32 | 8 | `entry = data_base + offsets[idx]` |
| `FUN_001c5eac` | 80 | 6 | sub-entry id reader (index records are `0x10` B, first byte = sub-entry id) |

Path templates in code: `talkevent/%s.evex`, `talkevent/movie/DEMO.evex`, `talkevent/rt_free/r_strt_op.ed`.
Dialog text on disc is **not** plaintext/zlib — use the GameFAQs script (`57905`) + RAM scene
match instead of a codec RE.

---

## 4. Globals (`DAT_`) — meaning and live RAM address

| global | RAM addr | meaning |
|---|---|---|
| `DAT_00397770` | `0x08B9B770` | static struct **beginning with a chapter-name table** (`one00`, `two00`, … stride `0x24`); first word `+4` = float `1.0`. **NOT the menu manager** — it is an *argument* passed to `FUN_002489d0` |
| `DAT_00392cd8` | `0x08B96CD8` | engine service / audio holder (its `+0x20` was menu audio, `-1` = not loaded) |
| `DAT_003925b0` | `0x08B965B0` | holds `&DAT_0132fb40` (a pointer written at run time) |
| `DAT_0132fb40` | — | the pad object |
| `DAT_0000e8a0` | `0x088128A0` | head of an intrusive singly-linked list |
| `DAT_00394400` | `0x08B98400` | sound-system handle (used with `"sound/snd_menu.scd"`) |
| `DAT_003a06a0` | `0x08BA46A0` | vtable installed at `pad+0x74` |
| `DAT_003982f0` | `0x08B982F0` | global modal owner for the (missing) localisation text→code link |
| `DAT_003915a0` | `0x08B915A0` | `FUN_000b1040` source |
| `DAT_003923c0` | `0x08B923C0` | resource holder: `+0x180`→`FUN_000e1c04`, `+0x790`→`FUN_000e4520`, `+0x4E0`→`FUN_000e2600` |
| `DAT_00391230` | `0x08B91230` | records of stride `0x4C` (`FUN_000717f8` selects three) |
| `DAT_0039b300/0039b304` | — | `PTR_s_general_archive_main_…` string pointers used by boot |

⛔ **A `DAT_`/`iRam` label in the RX range may be CODE, not data.** Dissidia's EBOOT is one big
RX segment (`0x0–0x3A6860`) plus a small RW segment; most of the upper range is BSS. Walking
`0x0000860C` produced `+0x3C = 0x03E00008` = `jr $ra`. **Compare file bytes vs live RAM bytes
before modelling any address**: equal ⇒ read-only constant (cannot hold state); different ⇒
something writes it.

---

## 5. Reader-relevant RAM anchors (measured live)

- **Load base** `0x08804000`; **readable span** `0x08800000`–`0x0A000000` (24 MiB).
- Board: `M = [0x08B98940] = 0x08C168F0`; `P = [M+0x118] = 0x09C11540`; cursor `D+0x194/+0x195`;
  origin `D+0x38[+2/+3]`; **DP logic** `R+6` (s16 signed); **DP display cache** `U+0xD78`.
- Fighters: `0x4ED0`-byte struct; tag-team has **FOUR** slots (stride `0x1A90`) — always derive
  "active" from `maxHP != 0`, never hardcode which slots are live.
- Lock field `fighter+0x2EC`: only ever `0` or the enemy pointer (see §6 for the retraction caveat).
- UI text: `0x09D16xxx`, `0x09E6Axxx`, `0x09E58xxx`, `0x08B7xxxx` (see §3.3).

---

## 6. Superseded / do-not-trust artifacts

| artifact | why |
|---|---|
| `stringrefs-report.txt` | produced against the **BinaryLoader** (`DISSIDIA`) project → `0.74`-skewed addresses → its **"0 candidate matches" is an ARTIFACT**, not an absence. Corrected run (`DISSIDIA_ELF`) found 23 occurrences / 10 functions. File already carries a superseded banner. |
| `loaders-report.txt` | same skew → its **"no references"** results are artifacts. Use `strings-report.txt`. File already carries a superseded banner. |
| §117 EX-core lock claim | **RETRACTED same day.** "The lock field can only hold 0 or the enemy" was drawn from the Ghidra *reports* (a subset), then a whole-binary scan contradicted it. The EX core is a **pickup that posts event `0x3C`** (`FUN_000ce824`), not a lockable object and not in the generic object list. |
| `DAT_000012c4` | is a **field offset**, not a global array — the widget is `P + 0x12c4`. |

**When a result is superseded, annotate the artifact file — do not delete it.** A recorded wrong
null with its cause is the strongest warning against repeating the mistake.

---

## 7. Open RE targets (so work does not re-tread)

1. **Menu logical selection index** — never found. Not in any of: the constructor arg (chapter
   table), `param_1[8] = -1` (that is the sound handle at `+0x20`), `DAT_00392cd8+0x20`,
   the three registered callbacks (draw), `FUN_00103c60` (an 8-byte accessor). The manager is
   **heap-allocated at menu-open time** (constructor allocates a `0x40c` object via
   `FUN_00247b40`) — resolve the returned pointer **while a menu is open**, do not keep re-reading
   statics. ⛔ Menu nodes are **freed and recycled** (`FUN_0024910c` free pool) → no stable node
   address; anchor on the parent static and the list walk, never a node address.
2. **EX core spawn storage** — ruled out as the lock field, the generic object list, and the env
   list. A fresh target, not a re-read.
3. **Board cursor world position / node id**; live tile-table `T` owner; the right-down-right
   `FUN_001c5bb4` read; item-name mapping.
4. **`FUN_003982f0`** — the global modal owner behind the localisation text→code link.
5. **Dialog codec** — deep path; the GameFAQs-script + RAM-match shortcut avoids it.

⛔ **Two recurring traps, stated for the next session:** (a) a resident **string** is not state
unless it is *absent* from the EBOOT (`region/storypoint.stp`, `talkevent/%s` etc. are all in
`EBOOT.dec` → finding them proves only that the code is loaded); (b) a series of RAM dumps is
only valid if every dump came from the **same screen** — a diff across a transition measures
nothing.

---

## 8. Source-of-truth documents

| document | contents |
|---|---|
| `~/oga-work/docs/reverse-engineering/dissidia-final-fantasy.md` | 9,656 lines, sections 1–122 + appendices; the primary narrative record |
| `~/oga-work/docs/reverse-engineering/CHECKPOINT.md` | toolchain versions, environment fixes |
| `~/oga-work/docs/reverse-engineering/codex-findings/` | `TASK*.md` / `ANSWER*.md` per RE question (board, DP, selection, etc.) |
| `~/oga-work/docs/research/dissidia-*.md` | per-screen host maps: battle setup, customize, name entry, EX burst, marker, quick battle, stage guide |
| `~/oga-work/docs/proposals/dissidia-battle-audio.md` | battle audio-cue spec + acceptance criteria |
| `~/oga-work/docs/data/dissidia_story_script.json` | 137 story-scene records (speaker + line), from GameFAQs script 57905 |
| `~/oga-work/scripts/psp-dissidia-*.py` | decode / unpack / menucursor / textdump / indexclass tooling |
| `...\oga-ghidra-dissidia\function-index.txt` | **generated** full function inventory (15,368 rows) |

*Regenerate the inventory:* `cmd.exe /c "...\oga-ghidra-dissidia\run-index.bat"`.
