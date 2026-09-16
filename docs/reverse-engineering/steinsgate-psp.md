# Steins;Gate: My Darling's Embrace (PSP) — reader investigation

**Status: SCRIPT FORMAT DECODED. DECOMPILE PIPELINE WORKING. CONFIG LOADER LOCATED.
LINE CURSOR STILL NOT FOUND.** A reader is now clearly reachable; it is not built.

## ✅ The decompile is DONE and the config loader is located

This is the furthest advance in this investigation. The full chain now works, and
Ghidra has analysed the executable:

| step | result |
|---|---|
| CSO → ISO | `maxcso.exe --decompress` → 1,385,979,904 bytes |
| ISO → EBOOT.BIN | 1,785,840 bytes, magic **`~PSP`** = encrypted PRX |
| EBOOT → ELF | `pspdecrypt -o EBOOT.dec EBOOT.BIN` → **valid MIPS ELF32**, entry `0x94224` |
| Ghidra import | `analyzeHeadless ... -processor "MIPS:LE:32:default"` → **analysis succeeded, 68 s** |
| functions recovered | **4,130** |

### ⭐ The way in: F `FUN_0000b990` — the SYSTEM.CFG loader

```
string 00121dbc "SYSTEM.CFG"        called from 0000b990 FUN_0000b990  (body 3472 bytes)
string 00121dc8 "error load system.cfg"   <- same function
string 00128690 "SYSTEM.DAT"        called from 00093bf8 FUN_00093bf8  (body 808 bytes)
```

**`FUN_0000b990` is where the game parses its own configuration**, and it is the
documented starting point for finding how scripts are indexed. Related loaders:

```
0000b990  FUN_0000b990   SYSTEM.CFG
00093bf8  FUN_00093bf8   SYSTEM.DAT
000a0bfc  FUN_000a0bfc   "load cpk : %s"
000a0f50  FUN_000a0f50   "load cpk : %s"
000a14d8  FUN_000a14d8   "Cpk Bind Work (%s)"
```

⛔ **The executable contains NO script text** (`Rintaro` 0 hits, `Mayuri` 0 hits) — the
text lives only in `DATA0.CPK`. So the reader must combine a static script parse with a
runtime cursor, exactly as the tooling here does.

### Reading the code

```bash
# open in the GUI
"C:\Users\Devin Prater\scoop\apps\ghidra\current\ghidraRun.bat"
# project: C:\Users\Devin Prater\oga-ghidra-sg   program: EBOOT.dec
```

⛔ **HEADLESS GHIDRA SCRIPTS MUST BE JAVA, NOT PYTHON.** PyGhidra is required for `.py`
scripts and it cannot install on this machine's Python 3.14. Use a `.java` script
(`scripts/SgQuery.java` is a working template) with `-scriptPath <dir> -postScript X.java`.

⛔ **THE PS ELF IS POSITION-INDEPENDENT: NO ADDRESS IS A LITERAL.** Searching the file
for an address's 4 bytes finds **nothing** (verified: 0 hits for a known address).
Addresses are built as `lui`/`addiu` HI16/LO16 pairs patched at load. The relocation
table is 8 sections of type **`0x700000A0`** (not `0x600000A0` — that guess made the tool
report "no relocations found", which reads as "not a PRX"), entsize 8, **44,606 entries**
(6191 R_MIPS_32, 18709 R_MIPS_26, 8748 HI16, 10958 LO16). `scripts/oga-mips-reloc.py`
reads it.


## ⛔ First: this is NOT the game the backlog entry meant

`docs/research/game-backlog-ranked.md` lists **"Steins;Gate — PSP — pure visual novel,
would be the easiest game here if a PSP core existed"**. The file in the library is a
**different title in the same series**:

| | |
|---|---|
| File | `Dropbox/Games/PSP/Steins Gate - Hiyoku Renri no Darling (English v0.5).cso` |
| Game ID (verified from the running game AND from `SYSTEM.CFG`) | **`ULJM06040`** |
| Title | **Steins;Gate: My Darling's Embrace** |
| Version | 1.02 |
| Also known as | Hiyoku Renri no Darling; "Darling of Loving Vows" |
| Type | **Fandisc** — a daily-comedy spinoff, not the serious sci-fi VN |

**The original Steins;Gate is `ULJM-05887`** (2011). The widely-cited dumped CWCheat
codes — including the debug-menu unlock `_L 0xD10E67E4 00000001` / `_L 0x110E67E4
00000004` and the alternative `0x0027C500 0x00000080` — are for **`ULJM05887`** and do
**not** apply. Do not carry them over.

Amusingly, `SYSTEM.CFG` lists `ULJM05887` *and* `ULJM99999DAT` among its strings, so the
engine is shared between the two titles; only the content differs.

## ✅✅✅ THE SCRIPT FORMAT — DECODED AND VERIFIED

This is the main result. The script is stored **uncompressed** inside
`PSP_GAME/USRDIR/DATA0.CPK` (a CRI `CPK ` archive), and in RAM as a byte-identical copy.

**Location:** found by searching the CPK for a line read off the in-game BACKLOG screen.
The "paper cups" line sits at **CPK offset `0x4422A2`**; the equivalent RAM copy is at
**`0x08AEB220`**.

### The record format

Records are **NUL-separated**. Inside a record, multi-byte Shift-JIS control pairs mark
the fields:

| bytes | meaning |
|---|---|
| `81 6B` | **speaker name follows** (`Rintaro`, `???`, ...) |
| `81 6C` | delimiter closing the speaker name |
| `81 67` | **the spoken line follows** |
| `81 43` | in-text delimiter — a comma/space break *inside* a line |
| `81 68` | end-of-box marker, followed by the literal text `%K%P` and a NUL |
| `00` | end of record |

⛔ **`%K%P` IS A PAGE BREAK, NOT TEXT.** It appears verbatim after every `81 68`. Left in,
a reader would speak the percent signs aloud mid-sentence. Strip it.

⛔ **A record with NO `81 6B` PAIR IS NARRATION** (inner monologue) and the game shows it
without a name plate. This is why the raw RAM view looked like lines beginning with odd
letters:

```
'gThe wise men of old...'      <- the 'g' is NOT a speaker code
'C Mayuri. Can I use...'       <- nor is the 'C'
```

Those were **unmapped byte values** (`81 67`, `81 43`) rendered as `latin1`. An earlier
pass in this investigation mis-read them as speaker codes and produced a table of
"speakers" (`g` = Rintaro, `C` = ???). **That reading was an artefact of the encoding and
is retracted.** The real speaker names are stored as text, and read cleanly:

```
@0x00442204  Rintaro   Accepting the unknown as the unknown is the first step towards God!
@0x0044225B  Rintaro   But in this case, by 'unknown'...
@0x00442293  ???       Hey, Mayuri. Can I use these paper cups?
@0x004422CC  ???       Um, sure. I guess so.
@0x0044238B  ???       Ooh, fried chicken! It looks so good!
```

This **matches the in-game BACKLOG screen exactly**, which is what verifies the parse.

### Working parser

`scripts/oga-sg-script.py` implements the format:

```
oga-sg-script.py DATA0.CPK --find "paper cups"
oga-sg-script.py DATA0.CPK --dump --from 0x442100 --count 20
```

## ✅ The decompile pipeline — WORKING

The earlier hypothesis (that this would need decompilation) was right, and the whole
route is now proven end to end:

| step | command / result |
|---|---|
| 1. CSO → ISO | `maxcso.exe --decompress in.cso -o out.iso` → 1,385,979,904 bytes |
| 2. Read the ISO | `scripts/oga-iso-extract.py` (minimal ISO9660 reader — see traps) |
| 3. EBOOT.BIN | `PSP_GAME/SYSDIR/EBOOT.BIN`, 1,785,840 bytes, magic **`~PSP`** = encrypted PRX |
| 4. Decrypt | `pspdecrypt` (built from https://github.com/John-K/pspdecrypt) |
| 5. Result | **valid MIPS ELF32**, `\x7fELF`, entry `0x94224`, machine MIPS R3000 |

```
pspdecrypt -o EBOOT.dec EBOOT.BIN
# "Decryption successful for tag D91613F0 with type 2"
```

`llvm-objdump -d --triple=mipsel EBOOT.dec` disassembles it correctly. Ghidra is not
strictly required — llvm-objdump works — but Ghidra will be far better for reading it.

**Relocation table:** the ELF is position-independent, so **no address appears as a
literal**. Searching for the 4 bytes of an address finds nothing — verified (0 hits for
the `SYSTEM.CFG` string address). Addresses are built as `lui`/`addiu` HI16/LO16 pairs
patched at load time. The table is 8 sections of type **`0x700000A0`**, entsize 8,
**44,606 entries** (6191 R_MIPS_32, 18709 R_MIPS_26, 8748 HI16, 10958 LO16).
`scripts/oga-mips-reloc.py` reads it.

## ⛔ THE REMAINING BLOCKER: nothing found says WHICH line is current

This is now the only thing between this work and a working reader.

What has been tested and ruled out:

| test | result |
|---|---|
| 4-byte words pointing into the RAM script block | **3** total, and only 3 even in other states |
| a word equal to the current record's address | **0 hits**, at every alignment |
| the script block changing between game states | **0 of 36864 bytes** — it is a load-time copy |
| pointers into it changing between states | **0** |

The script block is a **fixed, unchanging copy**, so it structurally cannot indicate the
current line. Whatever tracks position is elsewhere — most likely a (script-file, offset)
pair or an index into a script table, since the game clearly loads scripts from the CPK
rather than keeping a pointer into RAM.

**Why this cannot be guessed:** a reader that infers the line from table order announces
the *wrong line* with full confidence. For a blind player that is worse than silence.

## Tooling built

| script | purpose |
|---|---|
| `psp-probe.mjs` | `status` / `dump` (24 MiB user RAM) / `scan` / `press` / `shot` |
| `psp-walk.mjs` | press a plan, capture a shot + RAM dump per step (`wait:N` presses nothing) |
| `psp-burst.mjs` | press once, capture a rapid series, hash each frame (`--button wait` watches) |
| `psp-input-check.mjs` | prove injection reached `sceCtrl` via the `input.buttons` broadcast |
| `oga-psp-analyze.py` | find text + keywords in a RAM dump |
| `oga-psp-script.py` | locate the script block; hunt a current-line pointer |
| `oga-psp-diff.py` | which RAM blocks changed between two dumps |
| `oga-psp-dup.py` | find copies of script lines outside the block |
| `oga-iso-extract.py` | minimal ISO9660 reader (list / cat / save) |
| `oga-sg-script.py` | **parse the script format** |
| `oga-mips-xref.py` | find MIPS code building an address |
| `oga-mips-reloc.py` | read a PSP ELF's relocation tables |

⛔ **None of these write emulated memory.** They read, and they press the player's own
buttons.

## ⛔ Control facts (verified — saves the next session hours)

| button | effect |
|---|---|
| **START** | title screen. ⛔ **THE "Press START button" PROMPT BLINKS** — 12 presses at ~420 ms spacing can all land in the dark. **MASH to get in.** |
| **Cross** | advance dialogue |
| **Triangle** | **opens the BACKLOG** — a scrollback of recently spoken lines. ⭐ the best screen for this work: deterministic, always shows real dialogue, prints lines in on-screen order. |
| **Square** | opens a menu |
| D-pad | moves a cursor in menus; ⛔ does **not** scroll the BACKLOG (it is static) |

Other traps that cost time:

- **`--debugger=PORT` alone is not enough.** The effective `ppsspp.ini` had
  `RemoteISOPort = 0`, `RemoteDebuggerOnStartup = False`, `RemoteDebuggerLocal = False`,
  so no port was ever bound. Set all three and restart.
- **Only ONE debugger client can hold the connection**; a second gets `version timed out`.
- **`input.buttons.press` takes `button` (singular)** and **`duration` in FRAMES, not ms**.
  The request answers only *after* the hold completes.
- **`FullScreen = True`** in `[Graphics]` removes the window chrome entirely, giving a
  clean capture. A 52 px crop was not deep enough and left `File/Emulation/Debug` in frame.
- **`PrintWindow` needs flag 2 (`PW_RENDERFULLCONTENT`)**, in-process via ctypes. A plain
  BitBlt returns black on a flip-model swapchain.
- **The capture is letterboxed** — 1706×1066 with a 74 px black bar; game content is
  1706×967 = exactly 480:272.
- **Build a PPM by concatenating header and body**, never by pre-computing a header
  offset: the header length varies and PIL reports the mismatch as "image file is
  truncated", which reads as a capture fault rather than arithmetic.
- **`tasklist /FI "IMAGENAME eq ..."` can return nothing** on this host while
  `tasklist | grep -i ppsspp` works. MSYS mangles `taskkill //F` — use
  `cmd.exe /c "taskkill /F /IM PPSSPPWindows64.exe"`.
- **Native tools reject MSYS-style paths.** `7z` failed on `/c/Users/...`; pass
  `C:/Users/...`. This is why `oga-iso-extract.py` exists rather than using 7z.
- **`pspdecrypt`'s output flag is `-o` / `--outfile`.** `-d` is not it (that is a PSAR
  option), and the default output is `<input>.dec` next to the input.
- **PSP heap addresses move between sessions** — the same pointer sat at `0x08D3A0A8` in
  one dump and `0x08CE20A8` in another. Never hard-code one. The `0x08AEA000` script
  block is the exception (stable across dumps).

## Where to pick up

1. The script format is **done** — build the text half of a reader against
   `oga-sg-script.py`.
2. For the cursor: work in **Ghidra on `EBOOT.dec`** (MIPS, load base 0). Look for code
   that reads the CPK script region. The `SYSTEM.CFG` loader is a way in — EBOOT contains
   the strings `SYSTEM.CFG`, `SYSTEM.DAT` and `error load system.cfg` (vaddrs
   `0x121DBC`, `0x128690`, `0x121DC8`), so the loader that parses the config is findable.
3. Alternatively, dump RAM at a moment when the line **demonstrably changes** and diff
   against the `0x09B31000` duplicate region — that region is the best remaining RAM lead.


## ✅ The BOOT SEQUENCE and the engine's data layout — read from the decompile

`FUN_0000b990` is the engine's system init. Decompiling it produced a complete map of
how this game gets its data into RAM — which is what the cursor work needs.

```c
FUN_00094d08("start system init.\n");
_LAB_00055e7c = FUN_000a9820(0x100000, "Work Buffer");   // a 1 MB work buffer
FUN_000a9728(&DAT_00181f80, 0x1000000);
...
iRam00059d68 = FUN_000a9820(0x1000, "Sytem Config Buffer");   // 4 KB config buffer
while (FUN_000a0bfc("SYSTEM.CFG", iRam00059d68, 0x1000) == -1)
    FUN_00094d08("error load system.cfg\n");
iRam00055e34 = iRam00059d68;                    // ⭐ THE CONFIG BASE POINTER
```

### The SYSTEM.CFG format — decoded and confirmed against the file

The loader immediately reads **four u16 offsets at `+0x50`, `+0x52`, `+0x54`, `+0x56`**
and turns each into `base + offset`:

```c
if (*(short *)(iRam00055e34 + 0x50) != 0)
    iRam00059d70 = iRam00055e34 + (uint)*(ushort *)(iRam00055e34 + 0x50);
```

Checked against the real file — the table is an **array of 6-byte entries**, each entry's
first u16 being the offset to a string:

```
+0x50: 0x06F3 -> "pfs0:"     +0x52: 0x06F9 -> "pfs0:"
+0x54: 0x06FF -> "pfs0:"     +0x56: 0x0705 -> "pfs0:"
+0x58: 0x070B               +0x5A: 0x070D     +0x5C: 0x070F   +0x5E: 0x0711
```

The tail of the file holds Shift-JIS menu strings, e.g.
`初期設定の変更を行います。` ("Change the initial settings"). Earlier entries in the
table (`+0x40..+0x44` = `0x06D0/0x06DD/0x06E7`) are more names.

### After the config: the data archives

```c
FUN_000a2810(1, "DATA0.AFS", 0x20, 0x10);
FUN_000a2810(2, "DATA1.AFS", 0x20, 0x10);
local_30 = "afs0:/"; local_2c = "afs1:/";
FUN_000a0a40(&local_30, 3);
```

So `DATA0.CPK` / `DATA1.CPK` are opened and mounted as **`afs0:/`** and **`afs1:/`**. The
script text sits inside them uncompressed.

### Where things land in RAM at runtime

| what | address |
|---|---|
| **EBOOT runtime load base** | **`0x08804000`** — derived by finding unrelocated strings (`SYSTEM.CFG`, `SYSTEM.DAT`, `error load system.cfg`) in a live dump, all three agreeing |
| **loaded SYSTEM.CFG (all 2662 bytes)** | **`0x0933C580`** |
| **the config-base POINTER** | **`0x089B5E34`** = `0x0933C580` |
| the RAM script block | `0x08AEA000` (unchanging) |
| script duplicate | `0x09B31000`+ |

⭐ **`0x089B5E34` is a confirmed engine global holding the config pointer** — it reads
exactly `0x0933C580`, the config, and sits inside the ELF's data segment (vaddr
`0x1B1E34`). That is a working anchor for further state-block work.

## ⛔ Two addressing traps that wasted real time

**1. A PSP PRX is loaded at a base other than 0, and the base must be derived, not
guessed.** Here it is `0x08804000`. Guessing `0x08800000` gives a plausible-looking
result that is wrong by 16 KB, and code then reads as data.

**2. Ghidra's `iRam<addr>` / `_global_` names for GP-relative accesses did not agree
with the runtime addresses on this binary.** Ghidra labelled the config-base global
`iRam00055e34`, but the value is actually at vaddr `0x1B1E34` (`0x089B5E34` at runtime).
Attempts to reconstruct `$gp` from Ghidra's naming produced addresses that read as zero.

**Therefore: cross-check any Ghidra global against a live RAM dump before trusting it.**
A string that survives load (because relocations never touch it) is the reliable way to
establish the load base; derive from that, not from Ghidra's address labels.

## ⛔ STILL BLOCKED — the line cursor

Everything above is new and solid, but it does **not** yet answer "which line is on
screen". Confirmed dead ends, from live dumps:

| probe | result |
|---|---|
| pointers to the RAM script block base | **0** |
| pointers to the duplicate region base | **0** |
| pointers to the current dialogue record address | **0** |
| words equal to a plausible block-relative offset | **16,118** — a coincidence generator, not evidence |
| the script block changing between game states | **0 bytes** |

The config base (`0x089B5E34`) and the four config-section pointers are the next things
to follow: whichever section the script system registers with is where a script table —
and therefore a line index — will be reachable from.


## Controlled BACKLOG experiment — result, and a hypothesis for `0x09B31000`

Ran a controlled sequence (open backlog → advance → reopen) with a RAM dump at every
step. Results:

| step | screen | RAM hash |
|---|---|---|
| triangle | game | `a2a5f5357d` |
| wait | game | `a2a5f5357d` |
| triangle | game | `a2a5f5357d` |
| cross | game | `a2a5f5357d` |
| cross | **BACKLOG** | `1d8912244b` |
| wait | BACKLOG | `1d8912244b` |
| triangle | game | `a2a5f5357d` |
| wait | game | `a2a5f5357d` |

Two clean, reproducible screen states — the game screen and the backlog — and the
hashes separate them perfectly. **But the backlog content was identical at both opens**,
so the story did not advance during this sequence. That makes the comparison
inconclusive for the cursor, again for the same reason as before: nothing changed.

⛔ **A CONTROLLED EXPERIMENT IS ONLY CONTROLLED IF THE VARIABLE ACTUALLY MOVES.**
Repeating the same screen with no state change produces two identical dumps and a null
result that looks like a failed method. Before a diff-based hunt, prove the variable
changed — e.g. by reading two *different* lines off the screen — not merely that two
different buttons were pressed.

### ⭐ Hypothesis for `0x09B31000` (untested, for the next session)

The duplicate script region at `0x09B31000` holds lines **in display order**. The most
likely explanation is that it is **the backlog's own storage** — a running history of
displayed lines, which is exactly why it is ordered and why it grows.

If so it is *list storage*, not a cursor, which would explain why no pointer into it
tracks the current line. Test: dump RAM with the backlog showing N lines and again with
N+2 lines, and check whether this region grew by two records. If it did, the cursor is
elsewhere and should be hunted in the engine state block (`0x089B5E34` and the config
sections), not in this region.

## ⛔ Note on the ISO/CPK on disk

`DATA0.CPK` (778 MB) and the decompressed ISO (1.39 GB) were written to
`%LOCALAPPDATA%\Temp\psp-iso` and `%LOCALAPPDATA%\Temp\psp-extract` for this work.
**They are game data and must never be committed or uploaded** — the project's rule is
that players supply their own ROMs. The repo's `stage-repo.sh` allow-list and
`check-no-roms.sh` enforce this; nothing from these directories is staged.
