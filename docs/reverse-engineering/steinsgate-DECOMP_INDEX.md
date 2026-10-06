# DECOMP_INDEX.md — Steins;Gate: My Darling's Embrace (PSP)

Address → meaning map for the Ghidra decompilation. **Search this first** before touching RAM.
`size`/`callers` are measured from the Ghidra project (`function-index.txt`); meanings cite the notes.

> **Placeholder resolved.** The workspace directive referenced `<DECOMP_PATH>` / `<ISO_PATH>`
> literals. No such paths exist. The real artifacts are in §1.

**Status: reader BUILT and PROVEN LIVE** (`scripts/psp-sg-live.mjs`) — dialogue reads from the
running game, the story advances, each line is spoken in order. The remaining work is
**integration** (no in-app PSP core; it is a CLI), not reverse engineering.

---

## 1. Paths and entry points (the real `<DECOMP_PATH>`)

| what | path |
|---|---|
| **PRIMARY decomp workspace** | `C:\Users\Devin Prater\oga-ghidra-sg\` (project name **`SGProj`**) |
| program inside the project | **`EBOOT.dec`** (MIPS ELF32, entry `0x94224`) |
| machine-readable function dump | `...\oga-ghidra-sg\function-index.txt` (**4,130 functions**) |
| ISO (runtime verification only) | `maxcso.exe --decompress` → 1,385,979,904 bytes |
| EBOOT.BIN | 1,785,840 bytes, magic `~PSP` (encrypted PRX) |
| notes | `~/oga-work/docs/reverse-engineering/steinsgate-psp.md` |
| live reader | `~/oga-work/scripts/psp-sg-live.mjs` |

⛔ **Load base is `0x08804000`** — derived by finding unrelocated strings (`SYSTEM.CFG`,
`SYSTEM.DAT`, `error load system.cfg`) in a live RAM dump, all three agreeing.

⛔ **Ghidra's names on this binary are offset by a constant.** The translation that checks out on
an independently verified value:
```
real_RAM = LOAD_BASE + ghidra_name + 0x15C000
verified: iRam00055e34 -> 0x08804000 + 0x55e34 + 0x15C000 = 0x089B5E34 = the config pointer
```
**Validate any translation against a value you already know before trusting it** — three earlier
attempts that derived the rule theoretically all produced addresses that read zero.

---

## 2. How to query the decomp (the pipeline, verified)

```
.cso --maxcso --decompress--> .iso
.iso --extract PSP_GAME/SYSDIR/EBOOT.BIN--> (1,785,840 B, '~PSP')
     --pspdecrypt -o EBOOT.dec--> valid MIPS ELF32 (entry 0x94224)
     --analyzeHeadless -processor "MIPS:LE:32:default"--> SGProj / EBOOT.dec (68 s)
```

- `run-index.bat` (in the project dir) regenerates `function-index.txt`.
- Per-report scripts: `scripts/SgQuery.java`, `SgDecomp.java`, `SgLog.java`, `SgPhone.java`.
- All Ghidra scripts must be **Java** (PyGhidra will not install on this machine's Python 3.14).

---

## 3. Function index — by system

Format: `FUN_addr  size  callers  meaning`.

### 3.1 System init / config loader — the way into the engine

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_0000b990` | 3472 | 1 | **system init / SYSTEM.CFG loader**; 1 MB work buffer, 4 KB config buffer, sets the config global |
| `FUN_000a9820` | 316 | 15 | **the named allocator** — `alloc(size, "Name")`; a struct definition hiding in a call |
| `FUN_000a9728` | 116 | 1 | init helper (`&DAT_00181f80`, `0x1000000`) |
| `FUN_000a0bfc` | 852 | 1 | file loader (`"SYSTEM.CFG"`, retries until `!= -1`) |
| `FUN_00094d08` | 96 | 65 | error/log printer (`"error load system.cfg\n"`) |
| `FUN_00093bf8` | 808 | 4 | **`SYSTEM.DAT` loader** — parses the game's own file table |
| `FUN_000a2810` | 916 | 2 | AFS mount (`"DATA0.AFS"`, `"DATA1.AFS"`) |
| `FUN_000a0a40` | 248 | 1 | mounts the archives as **`afs0:/`** / **`afs1:/`** |
| `FUN_000a14d8` | 452 | 1 | `"Cpk Bind Work (%s)"` |
| `FUN_000a0f50` | 1160 | 1 | `"load cpk : %s"` |
| `FUN_000a66d4` | 152 | 38 | **entry text renderer** (`render the entry's text`) |

### 3.2 Message log / backlog (the reader's target)

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_00051574` | 76 | 1 | **allocates the MessageLog buffer** — `alloc(config[+0x18] * 0xAC, "MessageLog Buf")` |
| `FUN_00052fa4` | 2040 | 1 | **backlog scroll logic** (reads `FUN_00051028(i, cur+i)`) |
| `FUN_00051028` | 376 | 4 | **entry renderer** — hands over the display-list layout (`param_1 = i * 0x2c`, base `0x18d04`) |
| `FUN_00050fc8` | 96 | 1 | **log index → entry pointer** (`FUN_00050fc8(index)`) — the current-line value goes in here |
| `FUN_00051520` | 84 | 1 | render call `(state, total-(cur+per_screen), (total-cur)-1)` |
| `FUN_0005126c` | 344 | 1 | scroll position set/clamp (`cur - 1`) |
| `FUN_000513c4` | 348 | 1 | scroll position set/clamp (`cur + 10`) |
| `FUN_0008d7f0` | 3360 | 0 | **the Phone Trigger's state bytes** |

### 3.3 Engine text / string helpers
`BACKLOG`, `BackLog`, `MesLog`, `MesLogSave`, `MessageLog Buf`, `MsgLog Load No:%d  Id:%d` are the
engine's own debug strings — grep the binary for them, they name the structures. `alloc(count*stride,
"Name")` **is a struct definition**: it gives the stride (`0xAC`), the count source (config `+0x18` =
512), and a name.

---

## 4. The message-log structure — VERIFIED (live)

```
MessageLog buffer   0x09557C80   (pointer stored at 0x08978F14)
config pointer      0x089B5E34 = 0x0933C580 (the loaded SYSTEM.CFG; capacity at +0x18 = 512)
record stride       0xAC
write head          0x089797E8   (0 until the log is first allocated)
entry(index)        log_base + (write_head - (index-1) - 1) * 0xAC    (index 1 = newest)
```

Within an entry: speaker name at **`+0x1C`** (0x28 bytes, NUL-padded), spoken line at **`+0x48`**
(0x60 bytes, NUL-padded). The display list is at **`0x08978D04`**, stride `0x2C`.

### Control codes (all verified against live text)

| bytes | meaning |
|---|---|
| `81 67` | start of spoken text |
| `81 68` | end of box |
| `81 43` | in-line break → `, ` |
| `81 65` / `81 66` | emphasis quotes around a word → `'` |
| `81 6B` / `81 6C` | speaker-name delimiters |
| `%K%P`, `%P`, `%K` | **page break → a space (never spoken)** |

⛔ **A record with no `81 6B` is narration** (no name plate) — most lines are.
⛔ **The `g` / `C` prefixes you see in a raw ASCII read are `latin1` artefacts** of `81 67` /
`81 43` — **not speaker codes.** An earlier pass built a whole speaker table out of them and was
wrong. Decode the delimiters.
⛔ **Lines WRAP across records** — records 5/6/7 continue one logical line across three records.
A spoken line must be assembled from consecutive records.

### SYSTEM.CFG format
Four u16 offsets at `+0x50`, `+0x52`, `+0x54`, `+0x56`, each turned into `base + offset`. The table
is an **array of 6-byte entries** (first u16 = offset to a string):
```
+0x50: 0x06F3 -> "pfs0:"   +0x52: 0x06F9 -> "pfs0:"   +0x54: 0x06FF -> "pfs0:"   +0x56: 0x0705 -> "pfs0:"
+0x40..+0x44 = 0x06D0/0x06DD/0x06E7   (more names)
```
The tail holds Shift-JIS menu strings (e.g. `初期設定の変更を行います。`).

---

## 5. Reader tooling (built and proven)

| artifact | purpose |
|---|---|
| `~/oga-work/scripts/psp-sg-live.mjs` | the live reader — modes for output/TTS/auto-advance |
| the trigger | `write_head` increments — **observed moving** `0 → 1 → … → 434` |
| `--auto` | advances the story itself (`circle` presses on the same socket) |
| `--tts` | speaks each line in order (verified byte-identical to stdout) |

⛔ **The emulator can be FROZEN in stepping mode** — found the hard way, and it presents as "the
game is hung". Check the CPU is running before believing a null.
⛔ A **separate input connection** matters: reads suppress presses at ~1/30th on the same socket.
⛔ Watch timestamps mark press **COMPLETION** (they await the hold), so changes appear to "lead"
presses — subtract the hold duration before attributing a change to a press.

---

## 6. Open targets

1. **Integration** — run the reader from a phone/desktop UI and add a PSP core if it is ever to sit
   inside Open Game Access. The reading, trigger, story advance and speech are all proven.
2. **Choices** — the Phone Trigger's option text IS in memory (§ the doc's final section); the
   remaining piece is the mail-reply state machine, not the text.

---

## 7. Source-of-truth documents

| document | contents |
|---|---|
| `~/oga-work/docs/reverse-engineering/steinsgate-psp.md` | the full record (1,276 lines), including the four wrong conclusions and what each actually was |
| `~/oga-work/scripts/psp-sg-live.mjs` | the working reader |
| `...\oga-ghidra-sg\function-index.txt` | **generated** full function inventory (4,130 rows) |

*Regenerate the inventory:* `cmd.exe /c "...\oga-ghidra-sg\run-index.bat"`.
