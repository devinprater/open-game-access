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

## ⛔ Note on the ISO/CPK on disk

`DATA0.CPK` (778 MB) and the decompressed ISO (1.39 GB) were written to
`%LOCALAPPDATA%\Temp\psp-iso` and `%LOCALAPPDATA%\Temp\psp-extract` for this work.
**They are game data and must never be committed or uploaded** — the project's rule is
that players supply their own ROMs. The repo's `stage-repo.sh` allow-list and
`check-no-roms.sh` enforce this; nothing from these directories is staged.
