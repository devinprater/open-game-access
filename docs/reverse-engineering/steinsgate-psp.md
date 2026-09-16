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


## ⭐ The BUTTON SWEEP — the input question finally settled by measurement

The most expensive assumption in this whole investigation was "the game is ignoring my
input", and it had never actually been tested. `scripts/psp-try-buttons.mjs` settles it:
press each button once, hash the screen before and after, report which presses changed it.

**Result:**

```
cross      CHANGED      triangle   CHANGED
up         CHANGED      down       CHANGED
left       CHANGED      right      CHANGED
ltrigger   CHANGED      rtrigger   CHANGED
start      no change    select     no change    square  no change
```

⛔ **INPUT WORKS.** Eight of eleven buttons produce a visible screen change. So every
earlier "nothing happened" was a wrong-button or wrong-state problem, **not** a tooling
failure. Any future null result should be re-checked with this sweep before being
attributed to the game.

⛔ **`left` / `right` SCROLL THE BACKLOG.** Earlier this document recorded "the backlog
does not scroll (up/down do nothing)" — that was wrong: up/down move something else and
`left`/`right` are the backlog's page controls. This is the **controlled variable** the
cursor hunt needs: a known, repeatable change in which lines are displayed.

## The three text regions, now distinguished

| region | changes? | what it is |
|---|---|---|
| `0x08AEA000`–`0x08AF3000` | **never** (0 bytes, every dump) | the whole **script file**, loaded once |
| `0x09B31000`+ | **between runs, not within one** | the **current scene's** lines, in display order |
| `0x089B5E34` | (pointer) | engine global holding the config pointer |

The scene region holds **different** content from the script file — real later-scene
dialogue:

```
[NAME]Itaru|[TXT]Now? But the party's almost over.
[NAME]Mayuri|[TXT]Are you okay, Okarin?
[NAME]Luka|[TXT]O-Okabe-san...
[NAME]Kurisu|[TXT]Shut up, Okabe!
```

That is a later party scene, **not** the paper-cups scene still resident at `0x08AEA000`.
So `0x09B31000` is scene-local storage loaded per scene — which is consistent with it
being where the displayed lines live, and therefore the right place to look for the read
position once a scene can be made to advance.

⛔ **It did not change while scrolling the backlog within a run**, so the backlog read
position is NOT in this region. Either it is elsewhere, or the backlog re-reads a fixed
list per page. The next test is to scroll the backlog and look for a small changing value
(something in 0..426, the count of `81 68` end-of-box markers in the region) across the
`left`/`right` presses — now that there is a controlled variable that provably moves.

## Tooling added this round

| script | purpose |
|---|---|
| `psp-try-buttons.mjs` | **press every button, report which change the screen** — settles "does input work" |

⛔ **AND A NOTE ON THE LAST NULL RESULT.** A previous round ran a "controlled" sequence
and got a null — because the dialogue had never advanced, so nothing moved. The fix is
not a better diff, it is **proving the variable moved first**. `psp-try-buttons.mjs`
exists precisely so that assumption is never made again.


## ⛔ Backlog-scroll test — controlled this time, and still a NEGATIVE

Finally ran the cursor hunt with a **provably moving variable**: open the backlog, then
scroll it with `right` (which the button sweep proved works), dumping RAM at each step.

The scroll is real — **three distinct backlog pages**, by screen hash:

```
bls-03 (page 1)  79322491fd
bls-07 (page 2)  869f625236
bls-09 (page 3)  99b165a249
```

Searching the dumps for small values that track the page produced a promising-looking
hit — a family of counters at `0x08978D28` / `0x08978D80` reading `[3,4,5,5]` and
`[2,3,4,4]` across the four steps. Strictly monotonic, clean, exactly the shape a page
counter would have.

**Then the cross-check killed it.** Reading the same addresses on a *different* screen:

```
game sc-07-left   0x08978D28 = 3   0x08978D80 = 2     <- same values as a backlog page
backlog bls-03    0x08978D28 = 3   0x08978D80 = 2
```

Identical. So those addresses do not track the backlog page at all — they increment with
**activity** (allocations per button press), which is why they happened to advance in
step with the scroll. The surrounding structure is a repeating 0x80-stride record full of
`0xFFFFFFFF` and sizes — a resource/atlas table, not a UI cursor.

⛔ **A value that advances when you press a button is not necessarily the thing that
changed.** It can be counting the button presses. The only thing that separated the two
was reading the same address on a screen where the *intended* variable differed but the
*activity* was similar — which is why Rule 8 (sample on a static screen) exists.

## Honest status after this round

**Everything verified:**

| | |
|---|---|
| script format, incl. speaker names | decoded, parser written, matched to on-screen BACKLOG text |
| decompile pipeline | CSO -> ISO -> EBOOT (`~PSP`) -> MIPS ELF -> Ghidra, 4,130 functions |
| engine boot | mapped (work buffer, config buffer, DATA0/1.AFS -> `afs0:/`, `afs1:/`) |
| SYSTEM.CFG format | decoded and confirmed against the file |
| runtime addresses | load base `0x08804000`, config `0x0933C580`, config pointer `0x089B5E34` |
| input | characterised by sweep — 8 of 11 buttons act; `left`/`right` page the backlog |
| the three text regions | distinguished (script file / scene copy / engine global) |

**Not verified: the read position.** Candidates ruled out, each with a reason:

| candidate | why it failed |
|---|---|
| pointers into the script block | 0 exist |
| pointers into the scene region | 0 exist |
| word equal to the current record address | 0 at every alignment |
| block-relative offsets | 16,118 — coincidence generator |
| counters that advanced with scroll | also advance on the game screen — activity, not page |
| the script block itself | 0 bytes changed, ever — a load-time copy |

**There is no reader for this game, and I do not have a candidate left that passes a
cross-check.** The remaining honest routes are both large:

1. **Decompile the backlog renderer** in `EBOOT.dec` (project saved at
   `C:/Users/Devin Prater/oga-ghidra-sg`). It must read *something* to know which lines
   to draw. Start from the display-list code near the `0x08CE2xxx` region the diffs keep
   pointing at.
2. **Watch a value live while scrolling** rather than diffing snapshots — a small script
   that polls candidate addresses at ~4 Hz across a scroll, so a page counter can be told
   apart from an activity counter by *when* it moves.


## Live-poll results — and a CORRECTION of my own reading

`scripts/psp-watch.mjs` polls candidate addresses at ~150 ms while running a plan that
interleaves real presses with deliberate `wait` steps. Plan used:

```
t=0.44s  triangle      t=2.94s  right
t=4.95s  wait          t=7.48s  right      t=9.49s  wait
```

**Result — every candidate is noise, and two were mis-called at first:**

| address | behaviour | verdict |
|---|---|---|
| `0x08978D28` / `D54` / `D80` / `DAC` | **0 changes** across presses AND waits | differs only BETWEEN runs — per-session heap state, not a cursor |
| `0x089AA990` | toggles `16 <-> 17` repeatedly **during waits** | animation / blink counter |
| `0x089AACAC` | `15 -> 14 -> 15 -> 14` at t=0.15, 0.30, 0.60 s — **before `triangle` was pressed at 0.44 s** | animation |
| `0x089AAD18` | `1 -> 2 -> 1 -> 2` over the same 0.6 s with **no input** | animation |

⛔ **CORRECTION.** An earlier note in this investigation reported that `0x089AACAC` and
`0x089AAD18` "moved on presses but not on waits" and were "the only candidates that
survive the discriminator". **That was wrong.** It came from reading only the TAIL of the
tool's printed change list, which started at `t=0.60s` — after the first two toggles had
scrolled past. Reading the **full series** shows the values oscillating in the first
0.6 s with nothing pressed at all.

⛔ **READ THE WHOLE SERIES, NOT THE TAIL OF THE REPORT.** A change list truncated to the
last N entries silently drops the earliest changes — and the earliest changes are exactly
where an animation artefact shows itself, because they happen before your first press.

**So: zero candidates survive.** Six independent classes of candidate have now been ruled
out, each for a stated reason:

| candidate | why it failed |
|---|---|
| pointers into the script block | 0 exist |
| pointers into the scene region | 0 exist |
| word equal to the current record address | 0 at every alignment |
| block-relative offsets | 16,118 hits — coincidence generator |
| counters that advanced with scroll | also advanced on the game screen — activity |
| small values near the scene region | oscillate with no input — animation |

**The method is now sound; the search space is the problem.** Random/diff-based hunting
across 24 MiB has been exhausted. The next move must be to narrow the space first —
decompile the backlog renderer and read *what it reads*, rather than continue guessing
at addresses.


## ✅✅✅ BREAKTHROUGH — the MESSAGE LOG found: the backlog's own data, in code and in RAM

Decompiling the functions that reference `BACKLOG` / `MessageLog Buf` produced the
backlog's real data structure. This is the closest thing yet to a reader.

### From the code

```c
void FUN_00051574(void) {
  uRam00018f14 = FUN_000a9820((uint)*(ushort *)(iRam00055e34 + 0x18) * 0xac,
                              "MessageLog Buf");
}
```

**The message log is a flat array of `0xAC`-byte records.** The count comes from the
loaded config at `+0x18`. This is the backlog's storage, allocated by name.

The scroll logic is in `FUN_00052fa4`. Its state block keeps:

| offset | meaning (from the code) |
|---|---|
| `+0xc` | current position — `FUN_0005126c(cur - 1)` / `FUN_000513c4(cur + 10)` on scroll |
| `+0x10` | total entry count — the scroll stops at `total <= cur + 0xb` |
| `+0xe` | per-screen line count |
| `+0x34` / `+0x36` | the two values in `"MsgLog Load No:%d  Id:%d"` |

And it renders through `FUN_00051520(state, total - (cur + per_screen), (total - cur) - 1)`
— so **`+0xc` IS a line index**, and `FUN_00051028(i, cur + i)` reads consecutive entries.

### Verified against a live dump

| what | value |
|---|---|
| config global (`iRam00055e34`) | `0x0933C580` — **matches the config found independently**, confirming the address translation |
| `config+0x18` (log capacity) | **512** records → 512 x 0xAC = 88,064 bytes |
| **MessageLog buffer** | **`0x09557C80`** (pointer at `0x08978F14`) |

The buffer holds the actual dialogue text, as `0xAC`-byte records:

```
[  2] +0x48  'Entropy the origins rumble'
[  5] +0x49  'gOur time slowly ticks away from the moment '
[  6] +0x48  'of our birth to the moment of our death. It '
[  7] +0x48  'is finite.'
[  8] +0x4E  'C time itself does not flow from the pas'
[ 11] +0x49  'gThe wise men of old understood that the '
[ 14] +0x49  'gHe who knows that he is wise may drown in '
```

⭐ **These are the SAME lines as the script block at `0x08AEA000`, split into display
lines** — note records 5/6/7 and 8/9/10 each continue one logical line across three
records. That is exactly how a word-wrapped backlog stores entries.

⛔ **The `g` / `C` prefixes here are the SAME `latin1` artefacts** documented earlier —
`0x81 0x67` and `0x81 0x43` rendered as one byte, not speaker codes. Do not re-derive a
speaker table from these.

### Address translation that works

Ghidra's names for this binary are offset from the real vaddr. The mapping that checks
out on a verified value:

```
real_RAM = LOAD_BASE + ghidra_name + 0x15C000
```

Verified: Ghidra `iRam00055e34` → `0x08804000 + 0x55e34 + 0x15C000` = **`0x089B5E34`**,
which reads exactly the config pointer `0x0933C580`. **Always validate a translation
against the one value you already know before trusting it.**

## ⛔ What is still missing — now a much smaller problem

The reader needs the **current** entry index, and the state block for the log is **not
yet pinned down**. The pointer global is at `0x08978F14`, but the `+0xc` / `+0x10` fields
read 0 at the addresses tried — those candidates land in a different structure
(`$gp`-relative data), so the state block must be located properly rather than assumed.

**But the shape of the answer is now known**, which it was not before:

1. the log is a flat array of `0xAC`-byte records at a known pointer (`0x09557C80`),
   capacity known (512), and the text inside is confirmed readable;
2. the code that walks it is identified (`FUN_00052fa4`, reads `FUN_00051028(i, cur+i)`);
3. the index lives at **`state + 0xc`** with total at **`state + 0x10`** — so the only
   remaining step is to find `state`, and the pointer at `0x08978F14` is the anchor.

That is a bounded, specific search rather than a 24 MiB hunt. **A reader is now clearly
in reach for this game**, and this is the first point in the investigation where that can
honestly be said.


## ✅ The backlog's STRUCTURE is fully mapped — and the index is now the only unknown

Decompiling the backlog's own accessors named every piece. The reader problem is now one
specific value, not a mystery.

### `FUN_00051028(i, entry)` — the entry renderer, and it hands over the layout

```c
void FUN_00051028(int param_1, undefined4 param_2) {
  param_1 = param_1 * 0x2c;
  psVar6 = (short *)(param_1 + 0x18d04);          // ⭐ THE DISPLAY LIST, at a FIXED address
  puVar3 = (undefined1 *)FUN_00050fc8(param_2);   // param_2 = LOG INDEX -> entry pointer
  ...
  uVar2 = FUN_000a66d4((int)*psVar6, puVar3 + 0x1c);   // render the entry's text
  ...
  *(undefined1 *)(param_1 + 0x18d24) = *puVar3;
  *(undefined2 *)(param_1 + 0x18d1c) = *(undefined2 *)(puVar3 + 4);
  *(undefined2 *)(param_1 + 0x18d1e) = *(undefined2 *)(puVar3 + 2);
```

| what | where |
|---|---|
| **display list** | fixed at `0x18d04` (Ghidra) → **RAM `0x08978D04`**, **stride `0x2C`** |
| entry lookup | `FUN_00050fc8(index)` — **the log index goes in HERE** |
| text inside an entry | **entry `+0x1c`** |
| entry byte 0 | copied to display `+0x24` (a type/flag) |

### Verified against a live dump

| what | value |
|---|---|
| config global | `0x0933C580` (matches, again) |
| log capacity `config+0x18` | **512** records x `0xAC` = 88,064 bytes |
| **MessageLog buffer** | **`0x09557C80`** (pointer at `0x08978F14`) |
| **display list** | **`0x08978D04`**, 0x2C stride |

The log buffer holds the real dialogue:

```
[  2] +0x48  'Entropy the origins rumble'
[  5] +0x49  'gOur time slowly ticks away from the moment '
[  6] +0x48  'of our birth to the moment of our death. It '
[  7] +0x48  'is finite.'
```

Records 5/6/7 continue ONE logical line across three records — that is word-wrapping, and
it means a spoken line must be assembled from consecutive records.

⛔ **The `g` / `C` prefixes are `latin1` artefacts of `0x81 0x67` / `0x81 0x43`**, not
speaker codes. This was mis-read once already in this investigation; do not repeat it.

### The address translation that works

Ghidra's names on this binary are offset by a constant. Solved from an
**independently verified value** rather than derived:

```
real_RAM = LOAD_BASE + ghidra_name + 0x15C000
verified: iRam00055e34 -> 0x08804000 + 0x55e34 + 0x15C000 = 0x089B5E34 = the config ptr
```

Every subsequent lookup was then checkable in one comparison, and two more validated
(the log capacity read 512; the display list fell in the same structure).

## ⛔ THE ONLY REMAINING UNKNOWN: the log index

`FUN_00050fc8(index)` converts a log index into an entry pointer, and **what is passed to
it IS the current line**. That function has not been decompiled yet — it is the single
next step, and it is small.

Two candidate shapes, both cheap to test:

1. `FUN_00050fc8(i)` returns `log_base + i * 0xAC` — then the index caller is everything
   and can be read directly;
2. it indexes a table of pointers, in which case the index is still the value to watch.

Note the earlier scan found **no pointer into the log buffer** other than the one global,
which supports (1): the log is reached by arithmetic, not by a pointer table.

### Honest position

**No reader yet.** But for the first time the problem is bounded and named:

- the text: found, at `0x09557C80`, readable, cap 512
- the render path: identified, `FUN_00051028` via `FUN_00050fc8`
- the display list: found at `0x08978D04`
- **missing: the index**, one small function away


## ✅✅✅ SOLVED — THE READER WORKS

`scripts/oga-sg-reader.py` reads the dialogue out of a live RAM dump and prints it with
speaker names. Real output from the backlog dump:

```
dump         : .../ram-backlog.bin
log base     : 0x09557C80
write head   : 29  entries logged
config       : 0x0933C580   capacity 512 x 0xAC

  [  1] ???: Erm, my mom told me to bring this...
  [  2] ng about. This is what you wanted, right?
  [  3] ???: Here, I got you the snacks you were talki
  [  4] ???: Um, sure. I guess so.
  [  5] ???: Hey, Mayuri. Can I use these paper cups?
  [  6] Rintaro: But in this case, by 'unknown'...
  [  8] Rintaro: Accepting the unknown as the unknown is
  [ 11] Rintaro: Taking the known as known, and the unkn
```

**This matches the in-game BACKLOG screen line for line.** The speaker name comes from
entry `+0x1c`; the dialogue from entry `+0x48`.

### The formula, from the game's own code

```c
int FUN_00050fc8(int param_1) {                 // param_1 = log index
  if (param_1 == 0) return 0x18f18;             // "no entry" fallback slot
  iVar1 = (iRam000197e8 - (param_1 + -1)) + -1; // walk back from the write head
  if (iVar1 < 0) return 0x18f18;
  return iRam00018f14 + iVar1 * 0xac;           // log_base + i * 0xAC
}
```

| global | Ghidra | real RAM | meaning |
|---|---|---|---|
| log buffer pointer | `iRam00018f14` | `0x08978F14` | -> `0x09557C80` |
| **write head** | `iRam000197e8` | `0x089797E8` | **29** entries logged |
| log capacity | `config+0x18` | `0x0933C598` | **512** |

**Index 1 is the NEWEST line** — the walk counts back from the write head, which is
exactly how a backlog draws (newest nearest the input prompt).

### Two bugs found and fixed while building it

1. **The name and text fields are fixed-width and NUL-padded.** Cleaning the padded
   string before splitting at the NUL left the padding in the output and made every line
   unreadable. Split at the NUL *first*.
2. **`report()` printed the whole RAM buffer** because `__init__` stored the data but not
   the path — so the "dump:" header was 24 MiB of bytes. Store the path.

⛔ Both were cosmetic but each made the tool look broken. Worth noting because the second
one produced a 92 MB terminal capture before it was obvious.

## What remains to make this a live reader

The reader works on a **dump**. To make it live, poll three 4-byte globals over the
debugger each frame instead of dumping 24 MiB:

| poll | at |
|---|---|
| log base pointer | `0x08978F14` |
| write head | `0x089797E8` |
| config pointer | `0x089B5E34` |

Then `index 1` is the newest line and `index == previous write head + 1` means a new line
appeared — which is the trigger to speak. That is a few dozen lines of host code, using
the `psp-watch.mjs` client already written.

**Steins;Gate: My Darling's Embrace now has a working reader.** It has not yet been wired
to speech, and it has not been tested against a scene that advances (the story never moved
during this investigation), so the "new line appeared" trigger is designed but unproven.


## ⛔ The live trigger is UNPROVEN — measured, not assumed

Polled the write head (`0x089797E8`) live at 200 ms across a plan of ten Cross presses and
two `wait` steps:

```
distinct values across 98 samples: [29]
= completely static
```

The screen did not change either. So **the story never advanced under scripted input**, and
the "speak when the write head increments" trigger has never been observed firing.

### What that does and does not mean

| claim | status |
|---|---|
| the reader parses the log correctly | **PROVEN** — output matches the backlog screen |
| the log's addresses and stride are right | **PROVEN** — capacity 512 read from config, entries decode cleanly |
| the write head identifies new lines | **UNPROVEN** — it has never been seen to move |
| the reader is usable by a blind player | **NO** — it is a dump tool, not wired to speech |

### Two things worth checking before blaming input

1. **The engine's display/mode byte.** `cRam00056185` reads **0**. In `FUN_00051028` that
   byte is tested against `1` and `2` to decide name-plate rendering, so `0` is a state the
   renderer treats as disabled. It may indicate the game is not in a scene that consumes
   dialogue input.
2. **The earlier `start` trap.** A `start` press during the attract sequence can cycle a
   menu rather than enter the game (this happened once, producing the `1d8912244b` screen).
   The current state may be a scene/attract mode rather than playable dialogue.

⛔ **Do not conclude the trigger is wrong from this.** The correct reading is that the
trigger is untested because the state that exercises it was never reached. Getting the game
into a scene where a text box visibly advances is the prerequisite, and it is a game-driving
problem, not a reader problem.

## Where this leaves the game — final position

**Working:** `scripts/oga-sg-reader.py` reads Steins;Gate dialogue from a RAM dump, with
speaker names, verified against the in-game backlog. The full pipeline from an encrypted
PSP disc image to readable dialogue is documented and reproducible.

**Not working:** the live/host loop (poll the three globals, speak on change), the story
advance needed to test it, and anything inside the app (there is no PSP core; this is
host-side against PPSSPP only).

**Effort to finish, if the state problem is solved:** small — a few dozen lines using the
`psp-watch.mjs` client already written.


## ✅ THE TRIGGER IS PROVEN — the write head was observed 0 -> 1

The previous section reported the trigger as unproven because the head read 29 in every
dump. That was a **sampling error on my part**: I always dumped *after* a sequence had
settled, so I only ever saw the post-load value. Reading the head across *every* dump
taken shows the mechanism working:

| dump | action | write head | log struct | records |
|---|---|---|---|---|
| `c1-01-start` | START | **0** | `0x00000000` | `0x00000000` |
| `c1-04-wait` | (wait) | **0** | `0x00000000` | — |
| `c1-05-circle` | **CIRCLE** | **1** | `0x09557C80` | `0x00000000` |
| `c1-06-wait` | (wait) | **1** | `0x09557C80` | `0x088B2200` |

⛔ **CORRECTION TO MY OWN EARLIER REPORTING.** I said the trigger was unproven and the
story "never advanced." What actually happened is that the message log is allocated and
filled *lazily on the first dialogue event* — head `0` means the log does not exist yet
(`log struct = 0`). Across all 70 dumps: **every dump with head 0 has a null log struct,
every dump with head 29 has a live one.** The 0 -> 1 transition is the trigger firing.

The "29" that never moved was the state *after* the log was populated — a log that is
already full of the prologue. Sitting in that state and pressing Cross produces nothing
new because the story is not being advanced by Cross there.

### ⛔ And that "record array" pointer was a false lead

The log struct at `0x09557C80` has `+0x00 = 0x088B2200`, which looked like a pointer to a
separate record array. It is not:

- records at `0x088B2200` decode as **binary garbage**, not text;
- `0x09557C80` used DIRECTLY as the record base produces clean entries.

**`0x09557C80` IS the record base.** The `+0x00` field is part of whatever else the
allocator wrote there. The original reader was right; an attempted "correction" to follow
that pointer was wrong and has been discarded.

```
[ 1] @0x09558F50 '???'      '\x81gErm\x81C my mom told me to bring this...\x81h'
[ 5] @0x09558CA0 '???'      '\x81gHey\x81C Mayuri. Can I use these paper cups?\x81'
[ 6] @0x09558BF4 'Rintaro'  '\x81gBut in this case\x81C by \x81eunknown\x81f...\x81h'
```

⭐ **The lesson: a plausible pointer field is not evidence.** A candidate pointer must be
followed and its TARGET READ before it is believed — that is the only cheap test, and it
took one command.

### What the dump history proves about the trigger

The trigger to speak is **`write_head` increasing**. That is now supported by an observed
transition (0 -> 1), not just by reading the code. The full check when wiring it live:

```
poll 0x089797E8 (write head)
head > previous_head  ->  a new line was logged  ->  speak line(index 1)
```

Combined with `scripts/oga-sg-reader.py` (which decodes the entries), the reader is
complete apart from the host loop that polls instead of dumping.


## ✅✅✅ THE LIVE READER — `scripts/psp-sg-live.mjs`

Not a dump tool any more: this reads dialogue **straight off the running game** over
PPSSPP's debugger socket. No RAM dumps, no screenshots — three 4-byte polls per tick.

```
$ node scripts/psp-sg-live.mjs --backlog 12
Entropy the origins rumble
Rintaro: Our time slowly ticks away from the moment of our birth to the moment of our death. It is finite.
Rintaro: Yet, time itself does not flow from the past to the future: it simply exists supernaturally in the now. It is infinite.
Rintaro: The wise men of old understood that the greatest wisdom was oft spoke by those who didn't understand their own genius.
Rintaro: He who knows that he is wise may drown in his own wisdom, but he who does not will stay away from the water, because he can't swim.
Rintaro: Taking the known as known, and the unknown as unknown -- this is true understanding.
Rintaro: Accepting the unknown as the unknown is the first step towards God!
Rintaro: But in this case, by 'unknown'...
???: Hey, Mayuri. Can I use these paper cups?
???: Um, sure. I guess so.
???: Here, I got you the snacks you were talking about. This is what you wanted, right?
???: Erm, my mom told me to bring this...
```

**Read the prologue top to bottom and it is correct English prose.** That is the
verification: this text is a real translation the game displays, and it now comes out of
the running process intact.

### Modes

| flag | behaviour |
|---|---|
| (none) | **follow** — poll the write head, print each new line as it appears |
| `--backlog N` | print the last **N logical lines** and exit (chronological) |
| `--json` | one JSON object per line, for a TTS hook |
| `--speak` | plain text suitable for piping to speech |

### ⛔ SIX bugs found and fixed while building the live reader

Every one of these produced output that *looked* nearly right, which is why they are
worth recording:

1. **Quote markers were never mapped.** `0x81e`/`0x81f` wrap an emphasised word, so
   `unknown` printed as `eunknownf`. Mapped them to `'`.
2. **Wrapped records were printed as separate lines**, chopping sentences mid-word.
3. **The wrap point is a byte width, not a word width.** The engine stores the
   word-boundary space at the **END of the earlier chunk**:
   `'...the moment '` + `'of our birth'`. Trimming it produced `momentof`, `Itis`,
   `thegreatest`, `isthe`, `drown inhis`.
4. **Trimming on EVERY merge destroyed the next boundary.** A line wrapped three times
   is merged twice; trimming after the first merge glues `can't` + `swim.` into
   `can'tswim`. **Trim once, when the logical line is complete.**
5. **`--backlog` counted records, not lines** — the number asked for and the number
   printed disagreed.
6. **⛔ THE INDEX DIRECTION.** `k = write_head - index`, so **index 1 is the NEWEST
   record and increasing index goes BACK in time.** The first version walked the index
   downward, collecting the OLDEST records, and produced scrambled text like
   `Rintaro: Taking the known...the unknswim.ay away from the water...`. Walk **ascending**
   to collect newest-first, then reverse into chronological order for `join()`.

### The complete address set

| what | address | notes |
|---|---|---|
| write head | `0x089797E8` | 0 until the log is first allocated |
| log base (record array) | `0x08978F14` | `0` until the first dialogue event |
| config pointer | `0x089B5E34` | capacity at `+0x18` = 512 |
| record stride | `0xAC` | |
| speaker name | entry `+0x1C` | `0x28` bytes, NUL-padded |
| spoken line | entry `+0x48` | `0x60` bytes, NUL-padded |
| entry(index) | `log_base + (write_head - (index-1) - 1) * 0xAC` | index 1 = newest |

### Control codes (all verified against live text)

| bytes | meaning |
|---|---|
| `81 67` | start of spoken text |
| `81 68` | end of box |
| `81 43` | in-line break -> `, ` |
| `81 65` / `81 66` | emphasis quotes around a word -> `'` |
| `81 6b` / `81 6c` | speaker-name delimiters |
| `%K%P`, `%P`, `%K` | page break -> a space (never spoken) |

## Honest status of the Steins;Gate reader

| | |
|---|---|
| Disc image -> decrypted MIPS ELF | **done**, reproducible |
| Log structure from the game's own code | **verified** |
| Reads dialogue from the LIVE game | **working**, verified against real prose |
| Trigger (`write_head` increments) | supported by an observed 0 -> 1 |
| Wired to speech (TTS) | **NOT DONE** — `--speak`/`--json` expose the hook |
| Inside the app | **NOT DONE** — no PSP core; host-side against PPSSPP only |

⛔ **The one thing still not observed is the write head incrementing DURING dialogue.**
The 0 -> 1 transition was seen at log allocation. A full scene advance has not been driven,
because the story in the reachable state does not advance under scripted button input.
So the follow mode is built and runs, but it has not yet been seen printing a line in
response to the story moving.


## ✅✅✅ SOLVED AND PROVEN LIVE — the trigger fires, the story advances, the reader reads

The last open question was whether the write head moves **during** dialogue. It does, and
it is now demonstrated end to end. `--auto <button>` presses and polls on the **same**
debugger connection, advancing the story while reading it:

```
$ node scripts/psp-sg-live.mjs --auto circle
???: What do you mean by things you don't understand?
Rintaro: Is that so? I thought as much. This is more of the Organization's handiwork!
Rintaro: What's done is done. This too may be the will of Steins Gate. El Psy Kongroo.
I lowered my cell phone, and looked around the room to get a better grasp of the situation.
Mayuri: Phew! I was worried you'd forgotten me!
Right, this high schooler who looks like a middle schooler is Shiina Mayuri.
She's a member of this laboratory -- lab mem 002, to be precise -- and my childhood friend.
???: Yeah, I agree with Makise-shi. An Okarin who isn't weird isn't an Okarin at all.
Christina?: And I've told you just as many times not to call me 'Christina'!
Kurisu: I told you, my name is Makise Kurisu!
```

**Speaker plates come from the game itself** — `Rintaro`, `Mayuri`, `Kurisu`, `Christina?`.
Those names are not in any table I built; the engine supplies them per line, including
the `?` convention for a speaker the protagonist has not identified yet.

### ⛔ FOUR wrong conclusions I reached before this, and what each actually was

1. **"No line cursor exists"** (0 of 49 script-pointing words changed). Wrong question:
   the script block is static by design — the cursor is in the **message log**, not the
   script.
2. **"The write head is static, the trigger is unproven."** Wrong sampling: I always
   dumped *after* a sequence settled. The head moves 29 -> 30 -> ... -> 55 under play; a
   full scene produced **18 lines in one run**.
3. **"The story will not advance under scripted input."** Wrong **button**. `cross` does
   nothing in this state; **`circle` advances dialogue**. A button sweep (press each, watch
   the head) is what found it — guessing would not have.
4. **"The follower prints nothing, so the trigger is broken."** Wrong **topology**:
   PPSSPP's debugger is effectively **one client**. A separate presser process competed
   with the follower, so the head moved while the follower saw nothing. Pressing and
   polling must happen on **the same connection** — that is what `--auto` does.

### ⛔ The "second client connected" result is misleading

A probe that opened a second websocket reported "CONNECTED", which appears to contradict
the one-client rule. It does not: the second socket **connects but does not get answers**,
so `version` times out and the effect is a silent dead client. **Judge by whether a request
completes, not by whether the socket opens.**

### The button sweep — how the right button was found

| button | head moves? |
|---|---|
| `cross` | no |
| **`circle`** | **YES — advances dialogue** |
| `start` | no (opens a menu) |
| `square`, `triangle` | no |
| `up`, `down`, `left`, `right` | no |

Accepted button names on this build: `cross, circle, square, triangle, start, select, up,
down, left, right, home, hold, wlan, screen, note`. Rejected: `l, r, l1, r1, shoulder_l,
shoulder_r, L, R, volup, voldown, power`.

### ⭐ The method that actually solved it

**Press each candidate, watch the write head, keep the one that moves it.** Three
symptoms that all read as "the game is broken" — no cursor, frozen head, no advance — were
each a wrong *assumption* (wrong structure, wrong sampling, wrong button), and each was
settled by measuring instead of reasoning.

## ⛔ Note on the ISO/CPK on disk

`DATA0.CPK` (778 MB) and the decompressed ISO (1.39 GB) were written to
`%LOCALAPPDATA%\Temp\psp-iso` and `%LOCALAPPDATA%\Temp\psp-extract` for this work.
**They are game data and must never be committed or uploaded** — the project's rule is
that players supply their own ROMs. The repo's `stage-repo.sh` allow-list and
`check-no-roms.sh` enforce this; nothing from these directories is staged.
