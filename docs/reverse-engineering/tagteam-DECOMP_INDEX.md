# DECOMP_INDEX.md — DBZ: Tenkaichi Tag Team (PSP, `ULUS10537`)

Address → meaning map for the Ghidra decompilation. **Search this first** before touching RAM.
`size`/`callers` are measured from the Ghidra project (`function-index.txt`); meanings cite the notes.

> **Placeholder resolved.** The workspace directive referenced `<DECOMP_PATH>` / `<ISO_PATH>`
> literals. No such paths exist. The real artifacts are in §1.

**Status: game driven live to Main Menu + Character Select; system map found; 1P/2P HP VERIFIED LIVE
under attack; selection index located in code (`ctx+0x68`) but the ctx address NOT resolved;
player position found (REST-to-REST method); audio beacon built and validated. Adapter not written.**

---

## 1. Paths and entry points (the real `<DECOMP_PATH>`)

| what | path |
|---|---|
| **PRIMARY decomp workspace** | `C:\Users\Devin Prater\oga-ghidra-tagteam\` (project name **`TAGTEAM`**) |
| program inside the project | **`EBOOT.dec`** (MIPS-II ELF32) |
| machine-readable function dump | `...\oga-ghidra-tagteam\function-index.txt` (**7,497 functions**) |
| ISO (runtime verification only) | `.cso` 1,372,225,536-byte ISO (matches the CSO header) |
| EBOOT.BIN | 3,748,256 bytes, magic `~PSP` |
| notes | `~/oga-work/docs/reverse-engineering/dbz-tenkaichi-tag-team.md` (5,256 lines) |

### ⭐ THE ADDRESS MAPPING — pre-linked, unlike the other PSP projects
```
LOAD off=0x001018  vaddr=0x08804040  filesz=0x27D45D  flags=7
LOAD off=0x27F000  vaddr=0x08AEFB70  filesz=0x84      flags=6
```
The first LOAD segment's `vaddr` is already in the `0x08800000` range, so **the ELF is pre-linked to
absolute addresses and NO base offset applies**:
```
RAM = ELF_vaddr          (this game)
```
Compare: **Steins;Gate** needed `RAM = 0x08804000 + vaddr`; **Another Road** also measures
`0x08804000` (+ vaddr) — verified live 2026-10-06 from five independent strings.

⛔ **CORRECTION (2026-10-06): an earlier version of the line above said "Another Road needed a DERIVED
base (`0x0898036A`)". That was WRONG.** Re-derived by measurement — five strings of known ELF vaddr
(`%05ddmg`, `%02dHIT`, `[SYS] AUTO SAVE`, `data_sys_us`, `[TITLE]`) all landed at `base = 0x08804000`.
`0x0898036A` has no recorded derivation and is **not 4-byte aligned** (`% 4 == 2`), so it cannot be a
module load base. The real lesson still holds in a different form: **the two games are NOT
interchangeable** — Tag Team's ELF is pre-linked (`vaddr=0x08804040` in the file, `RAM = vaddr`, no
base added) while Another Road's is not (`vaddr=0x00000000`, `RAM = 0x08804000 + vaddr`), so the same
arithmetic does not apply to both even though their bases coincide.

**Never carry a mapping across games — derive it per build.**

Consequence for this title: a literal in the `0x08800000`–`0x0A000000` range **is** a RAM address
here, so globals are directly findable. (A scan
for such literals in instruction operands returned 0 — this build reaches globals via a small number
of base registers rather than absolute literals.)

⛔ **`7z` CANNOT OPEN A `.cso`** ("Cannot open the file as archive") — it is a PSP block-compressed
ISO. Use `open-game-access/scripts/oga-cso-extract.py`.
⛔ **The CSO header field at offset `0x10` is the BLOCK SIZE (2048), not a log2 shift.** Reading it
as a shift gives `1 << 2048` and surfaces as `zlib.error: incorrect header check` — reads as a
corrupt image, not an arithmetic bug.

---

## 2. ⭐ THE GAME'S OWN SYSTEM MAP — from its UI element names

Grepping the decompiled ELF for UI asset names named the systems outright, with **no RAM scanning**.
This is the highest-value trick found on this title:

| address | string | meaning |
|---|---|---|
| `0x08A72218` | `BTL_HP_MAIN_1P` | battle HP bar, player 1 |
| `0x08A723CC` | `BTL_HP_MAIN_2P` | battle HP bar, player 2 |
| `0x08A7224C` | `BTL_KIRYOKU` | **ki gauge** |
| `0x08A72A78` | `BTL_HARD_BATTLE` | hard battle mode |
| `0x08A733AC` | `chara_name_01` | character name plate 1 |
| `0x08A73384` | `chara_name_02` | character name plate 2 |
| `0x08A73578` | `30_select_ok_%d` | selection confirm |
| `0x08A739D0` | `charaname` | character name |
| `0x08A739FC` | `text_playername` | player name text |
| `0x08A73AD0` | `40_stage_select_cursor_%02d` | **stage-select cursor** |
| `0x08A73AF4` | `40_stage_select_tab` | stage-select tab |
| `0x08A75828` | `SKILLDATA_WINDOW` | skill panel |
| `0x08A76470` | `50_windows` | menu windows |
| `0x08A76538` | `gauge_attack` | stat gauge: attack |
| `0x08A76548` | `gauge_defense` | stat gauge: defense |
| `0x08A76558` | `gauge_technic` | stat gauge: technic |
| `0x08A76948` | `gauge_dpoint` | DP gauge |
| `0x08A76960` / `0x08A7696C` | `text_yes` / `text_no` | confirm dialog |
| `0x08A68A8C` | `disc0:/PSP_GAME/USRDIR/PACKFILE.BIN` | the data archive |

**Workflow that follows:** decompile the function calling `FUN_08840dc4(node, "gauge_attack")` — that
is the stat-panel renderer, and the accessor it calls returns the live value.

---

## 3. Function index — by system

Format: `FUN_addr  size  callers  meaning`.

### 3.1 Character select / UI panels

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_08a02a3c` | 1416 | 0 | **character-select panel builder** — the function that lays out the per-player character panels; holds the **selection index at `ctx+0x68`** (see §4) |
| `FUN_08a3c458` | 1116 | 3 | stat panel (`FUN_08a3c458 -> FUN_08840dc4(node, "gauge_*")`) |
| `FUN_08a3b5f4` | 2676 | 0 | page-reference candidate (4 refs) |
| `FUN_08a06068` | 768 | 0 | page-reference candidate (4 refs) |
| `FUN_08a29118` | 5684 | 1 | page-reference candidate (4 refs) |
| `FUN_08a46f84` | 1280 | 0 | page-reference candidate (4 refs) |
| `FUN_08a3c990` | 56 | 0 | panel caller |
| `FUN_08a3c8b4` | 220 | 0 | panel caller |
| `FUN_08a3c9c8` | 224 | 0 | panel caller |
| `FUN_08a3c320` | 124 | 1 | ⛔ **decompiles to `halt_baddata()`** ("Control flow encountered bad instruction data") — read the caller or the raw instructions |

### 3.2 Identity / name resolution

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_0883ee74` | 292 | 32 | **roster-name key resolver** — names are **keys**, not pointers |
| `FUN_08840dc4` | 60 | 91 | **node resolver** — `(node, "gauge_*")`; the stat-gauge accessor |
| `FUN_08a18730` | 3432 | 0 | ⚠️ **generic UI-node walker**, NOT a character-name resolver |
| `FUN_08a35ff0` | 3804 | 0 | page-ref candidate (pageRefs=9, resolverCalls=8) |
| `FUN_08a44ccc` | 5052 | 0 | page-ref candidate (pageRefs=14) |
| `FUN_089f728c` | 3892 | 4 | shows a paired-table structure |
| `FUN_088392bc` | 504 | 0 | pageRefs=4, resolverCalls=5 |
| `FUN_08838e3c` | 484 | 0 | pageRefs=3, resolverCalls=5 |
| `FUN_08a26400` | 2224 | 0 | **prime target** — references `0x08A75538`/`0x08A7553C` and calls the resolver |
| `FUN_08a2c314` | 1236 | 0 | referenced by `FUN_08a18730` (`0x3b8`) |

---

## 4. ⭐ THE SELECTION INDEX — found in the DECOMPILER (not RAM)

RAM sweeping failed; the decompiler answered it. `FUN_08a02a3c` (character-select panel builder)
contains:
```c
if (*(int *)(iVar2 + 0x58) == *(int *)(param_1 + 0x68)) { /* this panel is HIGHLIGHTED */ }
local_68 = (uint)(*(int *)(iVar2 + 0x5c + *(int *)(param_1 + 0x68) * 4) == 1);
```
| expression | meaning |
|---|---|
| **`ctx + 0x68`** | **the selected index** |
| `panel + 0x58` | that panel's own id, compared against `ctx+0x68` to decide the highlight |
| `panel + 0x5c + [ctx+0x68]*4` | per-item flag array indexed BY the selection |

✅ **Recognition pattern:** a comparison of the shape `panel_field == ctx_field` inside a layout loop
is a highlighter, and the `ctx_field` side is the selection index.

⛔ **`ctx` is a pointer argument — the index is `ctx+0x68` with NO fixed address**, so a RAM sweep
cannot find it. That is why the sweep failed. **Resolve `ctx` from the caller.**

### ⛔ `FUN_08a02a3c` is VTABLE-DISPATCHED — a general trap for this engine's UI code
```asm
08a02a44  or    s2,a0,zero     ; s2 = param_1  (the UI context)
08a02a48  lw    a0,0x40(s2)    ; a0 = ctx->vtable
08a02a4c  addiu a0,a0,0x40
08a02a50  lh    a1,0x0(a0)
08a02a54  lw    a2,0x4(a0)
08a02a80  jalr  a2             ; INDIRECT call through ctx
```
Consequences (both confirmed): **Ghidra finds NO callers**, and there are **no direct address
references** to the function either. When a UI/update function has no callers and no xrefs, stop
trying to reach it from the call graph — find the **context's allocation site** (a size-consistent
`malloc` whose result is stored to a global) instead.

---

## 5. Verified live / measured

| item | state |
|---|---|
| CSO → ISO → EBOOT → ELF → Ghidra | **done, verified** |
| Game driven to Main Menu + Character Select | **done, live** (screenshots kept) |
| System map (UI element names) | **found** (§2) |
| Mission titles + control tutorials (UTF-16LE, readable) | **found** |
| Roster names (UTF-16LE) | **found** |
| Character-select selection index | **located in code (`ctx+0x68`); ctx address NOT resolved** |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Player position | **FOUND** — the **REST-to-REST method** (sample AT REST; every earlier attempt failed) |
| Audio beacon (objective chevron bearing) | **built and validated**; bearing correct ~92%, outlier is a 180° ambiguity flip |
| Objective position | **NOT found** — both RAM routes exhausted (recorded as dead ends) |
| Adapter code | **NOT written** — and it must not be until addresses are confirmed live |

### ⛔ Traps that cost real time on this title
- ⛔ **`right` does not move the character-select cursor**, and `down` fails too — the screen is a
  poor target. A menu that does not scroll with a given button cannot reveal a selection index driven
  by that button; that is a different claim from "the cursor does not exist."
- ⛔ **No enemy markers to track** — the objective does not yield to a "constant" filter.
- ⛔ A "0 fixed vs 10 fixed" contradiction was a **phase artefact**: the runs started in different
  game states. Fix: a **PHASE GATE** in the tool (read `0x08B6A08C` and require a plausible world
  triple). **State the screen/phase before measuring.**
- ⛔ **`FUN_08a3c320` decompiles to `halt_baddata()`** — short accessor wrappers often do; read the
  caller.

---

## 6. Open targets (in priority order)

1. **Resolve the UI context address** — re-take a clean cursor series on the character-select screen
   ALONE (screenshot each step to prove the cursor moved), then re-run the `P+0x68` pointer search.
   If that fails, find the context's **allocation site** (a size-consistent `malloc` whose result is
   stored to a global).
2. **Fighter identity** — the text table is found (UTF-16LE); the index field is not.
3. **Objective position** — both RAM routes exhausted; needs a different approach or a different
   game-state.
4. **Reach a battle** to confirm the stat chain (currently BLOCKED on reaching a battle).

---

## 7. Source-of-truth documents

| document | contents |
|---|---|
| `~/oga-work/docs/reverse-engineering/dbz-tenkaichi-tag-team.md` | the full record (5,256 lines) incl. retractions and dead ends |
| `~/oga-work/scripts/psp-tt-*.mjs`, `scripts/psp-tt-stagesearch.mjs` | live drivers / search tools |
| `~/oga-work/scripts/TagTeam*.java` | the Ghidra query scripts for this title |
| `...\oga-ghidra-tagteam\function-index.txt` | **generated** full function inventory (7,497 rows) |

*Regenerate the inventory:* `cmd.exe /c "...\oga-ghidra-tagteam\run-index.bat"`.
