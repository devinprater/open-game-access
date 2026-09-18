# Dissidia Final Fantasy (PSP) — reverse engineering notes

**Game:** Dissidia Final Fantasy (USA)
**Game ID:** `ULUS10437` (confirmed from `PARAM.SFO` `DISC_ID`; also self-identifies in `PACKAGE.BIN`)
**Title string:** `DISSIDIA FINAL FANTASY` (PARAM.SFO `TITLE`)
**Disc version:** 1.00 · **PSP system version:** 5.50 · **Region:** 32768

Source image: `Dropbox/Games/PSP/1982 - Dissidia - Final Fantasy (USA).cso` (1,370,502,434 bytes)

> **No ROMs in the repo.** The disc image, the decompressed ISO, the extracted `EBOOT`, and every
> file pulled out of the archive are copyrighted game data and stay in `%LOCALAPPDATA%\Temp\`.
> Only scripts and notes are committed.

---

## 1. Why this game, and what the target is

Menu and text reading for a blind player. The same goal as the Tag Team work, moved to a title whose
menus the FAQ documents in detail, so there is an external reference to check semantic claims against.

GameFAQs references used for the menu structure:
* Dissidia Final Fantasy (PSP) — menu / battle system guides.

Reported menu structure the FAQ describes (to be verified against the game, not trusted):
main menu with **Story / Battle / Customize / Museum / Shop / Options / Data**, a **Battle Lobby**
with rules and stage selection, and a **Customize** area with **Ability / Accessory / Item /
Party Command / Friend Card** submenus.

Note the overlap with the archive's own file names, section 4 — the manifest independently lists
`ability.bin`, `accessory.bin`, `item.bin`, `party_command.bin`, `friend_card.bin`, `save_data.bin`.

---

## 2. Disc structure (verified)

CSO decompressed to ISO with `scripts/oga-cso-extract.py` → **1,646,657,536 bytes**.

```
/PSP_GAME/PARAM.SFO                    472
/PSP_GAME/SYSDIR/EBOOT.BIN         4,731,520    ~PSP encrypted (tag C0CB167C, type 1)
/PSP_GAME/SYSDIR/BOOT.BIN          4,731,180
/PSP_GAME/USRDIR/DATA/PACKAGE.BIN  660,183,040   the data archive
/PSP_GAME/USRDIR/DATA/PACKAGE_INFO.BIN  68,908   the index
/PSP_GAME/USRDIR/DATA/MODULE/LIBFONT.PRX    20,752
/PSP_GAME/USRDIR/DATA/MODULE/LIBSUPPREACC.PRX 24,868
/PSP_GAME/USRDIR/DATA/BGM/*.at3
/PSP_GAME/USRDIR/DATA/MOVIE/*
```

`LIBFONT.PRX` is a font module — text is drawn from a font, so a string table exists somewhere.

### EBOOT

Decrypted with `pspdecrypt` (WSL, `~/pspdecrypt-src/`) → `EBOOT.BIN.dec`, **4,731,180 bytes**,
`Decryption successful for tag C0CB167C with type 1`.

```
magic \x7fELF, class 1, data 1, machine 8 (MIPS), type 65440
entry   0x002DC6C8
phnum 2
  LOAD off=0x000074  vaddr=0x00000000  filesz=3,827,808  memsz=3,827,808   flags=5
  LOAD off=0x3A68D8  vaddr=0x003A6860  filesz=6,816      memsz=20,453,164  flags=6
```

**⚠️ NOT pre-linked.** `vaddr` starts at `0x00000000` and the entry is `0x002DC6C8`, unlike Tag Team
which was pre-linked at `vaddr = 0x08804040`. So Dissidia needs its **own** load base derived from the
running game — **never carry a base across games** (project rule). The first Ghidra import used
`-loader-baseAddr 0x08804000` as a placeholder; that must be confirmed against live RAM before any
address is trusted.

---

## 3. The archive pair (verified)

### PACKAGE.BIN header

```
02 00 00 00 | 01 00 00 00 | 75 73 00 00 | 05 00 00 00 | "ULUS10437" ...
```
The payload **self-identifies with the game ID**.

### PACKAGE_INFO.BIN header

```
18 02 | 09 20 | "packm" | 16 00 00 00 | 00 00 00 00
```
ASCII magic **`packm`** at offset 0x4. Contains **5741** three-word records from 0x10.

Record layout (measured):

| word | meaning | evidence |
|---|---|---|
| `w0` | cumulative **END** offset | strictly ascending: 2071746, 2493824, 4295189, … |
| `w1` | per-entry **length** | 157949, 180213, 157363, … |
| `w2` | a **second offset** into PACKAGE.BIN | extracting there yields real data |

`w2` extraction verified: rec[0] `@26956` = float triplets (3.0, 4.5, 0.05); rec[1] `@41356` =
readable ASCII `"T200_2P"`, `"p_eht200"`; rec[3] `@16588` = 64.0, 64.0, 0.2, 0.2; rec[13] `@17356`
= 6.0, 10.0, 0.05. Many `w2` values cluster just under `536,870,912` (`0x20000000`), half the file.

**Open:** the `w0 - w1` chain does **not** reproduce consecutive starts (0/1999 exact, 7 within one
sector), so the `w0/w1` grouping and the `w2` addressing describe different views. Which one names
entries is unresolved.

### MPK archives (decoded)

`MPK ` is a **named** archive. Structure at the magic:

```
4d 50 4b 20   "MPK "
06 06 07 20   version/flag bytes
0c 00 00 00   entry count
12 records x 16 bytes:  w0 = name offset (relative to the MPK HEADER)
                        w1 = data offset (relative to the NAME BLOCK)
                        w2 = size in bytes
                        w3 = 0
NUL-separated name block, one name per entry, in entry order
```

Verified two ways: `at + w0[0] = 14336 + 208 = 14544`, exactly where `"battle.bin"` begins; and the
successive `w0` gaps (11, 13, 11, 13, 17, 14 …) equal name length + 1, i.e. a cumulative name table.
`names_at + w1[0] = 14544 + 400 = 14944` is where the data begins, with each entry's
`offset + size` landing on the next entry's offset.

#### MPK #1 @14336 — the system archive (12 entries)

| n | name | offset | size |
|---|---|---|---|
| 0 | `battle.bin` | 14944 | 23048 |
| 1 | `objentry.bin` | 38000 | 34208 |
| 2 | `system.bin` | 72208 | 360432 |
| 3 | `mapentry.bin` | 432640 | 12188 |
| 4 | `manual_param.bin` | 444832 | 4878 |
| 5 | `ptcommand.bin` | 449712 | 9212 |
| 6 | `voice_priority_parameter.bin` | 458928 | 4816 |
| 7 | `resident.frr` | 463744 | 232111 |
| 8 | `snd_menu.scd` | 695856 | 252304 |
| 9 | `snd_common.scd` | 948160 | 62688 |
| 10 | `bgm_entry.bin` | 1010848 | 3708 |
| 11 | `se_override.bin` | 1014560 | 24628 |

#### MPK #2 @1040384 — **the menu archive** (17 entries)

| n | name | offset | size |
|---|---|---|---|
| 0 | `common.bin` | 1041248 | 2176 |
| 1 | `save_data.bin` | 1043424 | 2478 |
| 2 | `friend_card.bin` | 1045904 | 4884 |
| 3 | `ability.bin` | 1050800 | 33688 |
| 4 | `accessory.bin` | 1084496 | 18264 |
| 5 | `accessory_help.bin` | 1102768 | 64682 |
| 6 | `item.bin` | 1167456 | 16982 |
| 7 | `item_help.bin` | 1184448 | 36122 |
| 8 | `party_command.bin` | 1220576 | 1594 |
| 9 | `menu_pk_loading_seq_0.bin` | 1222176 | 7248 |
| 10 | `menu_pk_loading_gim_0.bin` | 1229424 | 66912 |
| 11 | `auto_save.gim` | 1296336 | 2396 |
| 12 | `replay_CO.gim` | 1298736 | 1372 |
| 13 | `ICON0.PNG` | 1300112 | 13024 |
| 14 | `ICON0_NEW.PNG` | 1313136 | 11312 |
| 15 | `menu_lpk_loading_seq_0.bin` | 1324448 | 50448 |
| 16 | `menu_lpk_loading_gim_0.bin` | 1374896 | 50368 |

**This is the menu.** `ability_help.bin` / `item_help.bin` are the help-text resources.

#### Other archives

`MPK ` magic also found at **1425408** (third archive) and there is a nested **`ARC`** container with
four-character tags and `(offset, size)` pairs:

```
ARC\x01 \x08 ...  then  "spec" @0x90 len 0x26F8 | "sklp" @0x2788 len 0x13C |
                       "nekp" @0x28C4 len 0x4E8 | "scrb" @0x2DAC ...
```

---

## 4. Menu text: where it is NOT, and where it probably is

### Menu LABELS are textures, not strings

A payload-wide ASCII scan found `Battle`, `Customize`, `Shop`, `Select`, `Start`, but dumping the
context shows these are **GIM/TM2 texture names**, not UI strings:

```
title_09_BattleLobby.tm2   .kuriki.   Mon Mar 16 20:14:52 2009   GimConv 1.41   MIG.00.1PSP
title_00_BattleCustomize.tm2
title_08_Shop.tm2
```

So Dissidia bakes its main-menu label graphics into textures. `MIG.00.1PSP` is the GIM container
magic; `.kuriki.` is the tool author; `GimConv` is Sony's texture converter.

### UTF-16LE menu words: none

A whole-payload scan for `Story`, `Battle`, `Customize`, `Options`, `Continue`, `New Game`, `Ability`,
`Accessory`, `Item`, `Friend Card`, `Party`, `Museum`, `Shop`, `Moogle`, `Menu`, `Data`, `Exit`,
`Select` as **UTF-16LE** returned **zero hits**. So the menu text is not stored as UTF-16LE (which is
how Tag Team stored its script).

### Prose scan of the whole 660 MB payload: nothing

`scripts/psp-find-text.py` scanned all 660,183,040 bytes in 16 MiB windows for sentence-shaped runs in
both encodings (min 26 chars, must contain a space). The top windows were all **font glyph tables** —
e.g. `@399571308` yielding `"#! DECBHIGFRSQPVWUTZ[YX^_]\efdcijhgmnlkqrpouvtsyzxw}~|{`, which is a
sorted glyph atlas, plus runs of `6 D O ? ,` from a bitmap. **No prose anywhere in the payload.**

### The `*_help.bin` encoding is not plaintext (unresolved)

| file | size | entropy | note |
|---|---|---|---|
| `accessory_help.bin` | 64682 | 6.727 b/B | u16 table head, ascending: 6091, 6154, 6229, 6302 … |
| `item_help.bin` | 36122 | 6.885 b/B | u16 table head: 1041, 1066, 1069, 1072 … |
| `accessory.bin` | 18264 | 6.712 b/B | u16 head: 2325, 2345, 2365, 2382 … |
| `item.bin` | 16982 | 6.619 b/B | u16 head: 917, 920, 930, 944 … |

Each begins with a plausible **u16 offset table** (ascending values pointing into the file), so the
container is a string table — but the string data itself is not plaintext.

Ruled out:
* **single-byte XOR** — best key `0xBF` gives only 41.1 % printable, 22 % letters (random-ish);
* **ECB block repetition** — 16-byte blocks show 3503 distinct of 4000, no repeated block (not ECB);
* **simple zlib/gzip** — entropy 6.6–6.9 b/B is far above compressed data (~7.99).

Found: **a 32-byte periodic structure**, autocorrelation match **0.342 at period 32** (vs ~0.03 for
every other period 1–64 except its multiple 64 at 0.236). A 32-byte cycle points at either a
32-byte-key stream cipher or a 32-byte record/bit-packed format.

**This is the open problem.** Two routes:
1. **Bit-plane packing.** Period 32 with ~29,295 u16 values and `max 65517`, `p90 61184`,
   `p99 65156` — a nearly-full 16-bit range looks like *bit-packed* glyph codes rather than cipher
   text. Test: split into bit planes and look for a low-entropy/constant plane.
2. **Find the decoder in the EBOOT.** Look for the routine that opens `accessory_help.bin`, and read
   what it does with the bytes. This is the reliable route and is what the Ghidra import is for.

---

## 5. Scripts added for this game

| script | purpose |
|---|---|
| `scripts/psp-iso-ls.py` | walk a PSP ISO9660 image's directory records (hand-written, no third-party deps) |
| `scripts/psp-dissidia-package.py` | inspect PACKAGE.BIN / PACKAGE_INFO.BIN headers and scan for strings |
| `scripts/psp-dissidia-index.py` | decode the `packm` index and search for a plausible record stride |
| `scripts/psp-dissidia-unpack.py` | read PACKAGE.BIN by the index; dump/scan payload regions |
| `scripts/psp-dissidia-mpk.py` | decode `MPK ` named archives and extract entries by name |
| `scripts/psp-find-text.py` | find sentence-shaped text in a large binary (UTF-16LE and ASCII) |

---

## 7. Decompilation (done)

Ghidra headless import + full auto-analysis of the decrypted `EBOOT.dec`:

* processor `MIPS:LE:32:default`, loader `BinaryLoader`, `-loader-baseAddr 0x08804000` (placeholder)
* analysis **succeeded in 71 s**, project saved at `C:\Users\Devin Prater\oga-ghidra-dissidia` (`DISSIDIA`)

> `analyzeHeadless.bat` **cannot be invoked from bash/MSYS when its path contains spaces**, and 8.3
> short names are disabled on this host. Fix: a wrapper `.bat` that sets `%USERPROFILE%` and calls it
> with proper quoting — `oga-ghidra-dissidia/run-analysis.bat`. Use it rather than fighting the shell.

### The EBOOT's own asset paths name the text resources

2364 readable strings in the EBOOT. The relevant ones:

```
text/JP/mess_pk_loading_0.bin
text/JP/menu/loading_movie.bin
general_archive/field/menu.bin
general_archive/field/JP/menu_lang.bin
general_archive/field/field.bin
general_archive/field/JP/field_lang.bin
language.bin
pause_help.bin      ap_bonus.bin      dp_bonus.bin      info.bin
command_battle.bin  ptc_auto.bin      battle_result.bin  result.bin
judgement.bin       super_skill.bin   replay.bin         brightness.bin
general_archive/party_command/%s.bin
```

The `text/JP/` and `*_lang.bin` names are the localisation text. **None of those byte strings occur
in `PACKAGE.BIN`** (searched: `menu_lang.bin`, `field_lang.bin`, `language.bin`,
`mess_pk_loading`, `text/JP` → 0 hits), so they are addressed through a container, not stored as
named members.

Also in the EBOOT: `sceLibFont`, `libfont.prx`, `SYSTEM FONT T2 / T3`, `SYSTEM_FONT::Draw`,
`VOLATILE_MEMORY_LOADER` — the font system, confirming text is drawn from a font module.

---

## 8. Complete archive survey (done)

Scanned all 660 MB for archive magics:

* **388 `MPK ` archives** — every one decoded; **2204 entries, 1747 distinct names**
* **1008 `ARC\x01` containers** — not yet decoded
* 0 embedded ELFs

Full name list written to `%LOCALAPPDATA%\Temp\dissidia-extract\all-names.txt` by
`scripts/psp-dissidia-names.py`.

### Extensions across all archives

`.gim` 731 · `.fep` 248 · `.feb` 247 · `.fxseb` 227 · `.scd` 159 · `.bin` 112 · `.mpk` 99 ·
`.sequence` 65 · `.se` 64 · `.gmo` 54 · `.objx` 52 · `.fobx` 32 · `.at3` 26 · (none) 25 ·
`.arr` 21 · `.enc` 13 · `.fin` 13 · **`.mes` 11** · `.png` 4 · `.frr` 1

### The menu resources, by name

| resource | archive | size | what it should hold |
|---|---|---|---|
| `ability.bin` | 1040384 | 33688 | ability list |
| `accessory.bin` | 1040384 | 18264 | accessory list |
| `accessory_help.bin` | 1040384 | 64682 | **accessory help text** |
| `item.bin` | 1040384 | 16782 | item list |
| `item_help.bin` | 1040384 | 36122 | **item help text** |
| `party_command.bin` | 1040384 | 1594 | party commands |
| `friend_card.bin` | 1040384 | 4884 | friend card |
| `save_data.bin` | 1040384 | 2478 | save data |
| `pause_help.bin` | 632422400 | 8214 | **pause menu help text** |
| `name.bin` | 632422400 | 1428 | name table |
| `battle_voice_name.bin` | 632422400 | 73612 | voice names |

A second copy of the whole menu MPK exists at **1425408** with *different sizes*
(`accessory_help.bin` 67876 vs 64682) — almost certainly the second language pack.

`.mes` files are **stubs**: all 11 are 44–184 bytes and mostly zero-filled (e.g. `one00.mes` is 44
bytes of `00`; `eht00.mes` holds only a `MAPS$` tag). Not the text.

### A second text lead: the "sequence" resources DO hold UTF-16LE

`menu_pk_loading_seq_0.bin` (7248 bytes) has an entropy of only **2.881** — very structured — and
openly begins with UTF-16LE:

```
80 80 80 ff | 74 00 70 00 75 00 71 00 | 00 00 ... | ff ff ff ff ...
            ^^^^^^^^^^^^^^^^^^^^^^^^^^^  UTF-16LE "tpuq"

... 89 00 68 00 8a 00 69 00 | 00 00 ...
```

Note the codes at/above `0x80` (`0x89`, `0x8A`), which are **not ASCII** — consistent with the
`SYSTEM FONT T2 / T3` and `sceLibFont` strings in the EBOOT, i.e. a font that assigns the upper
range. The file also carries RGBA colour values (`80 80 80 ff`, `78 64 b0 ff`, `65 59 99 ff`) and
float pairs, so these are **layout+text records**, not plain string tables — which matches
`menu_pk_loading_seq_0.bin` being the payload of the EBOOT's `text/JP/mess_pk_loading_0.bin`.

Read them with `scripts/psp-dissidia-seqtext.py`.

### `ARC` containers decoded

The `ARC\x01` record layout is now confirmed (see section 8 fix): 16-byte header
(`ARC\x01`, u32 count, 2 reserved), then `count` × 16-byte records of
`(u32 flag, 4-char tag, u32 offset, u32 size)`, offsets relative to the ARC header.

```
ARC @14736   (8 entries)
  spec @144 len 9976 | sklp @10120 len 316 | nekp @10436 len 1256 | scrb @11692 len 184
  camr @11876 len 972 | chrg @12848 len 5300 | atkp @18148 len 428 | scra @18576 len 4472

ARC @43943936 (8 entries)
  R_FF @144 len 19080 | r_ff @19224 len 5946 | snd_ @25216 len 215280
  even @240496 len 572 | comm @241072 len 21200 | comm @262288 len 34072
```

### `.enc` / `.fin` / `.arr` per chapter — plaintext numeric, despite the names

Each story chapter has a triple, e.g. `one00.enc` (3568 B) + `one00.arr` (100 B) + `one00.fin`
(20 B), and `eht00.*`, `org00.*`. **The `.enc` extension does not mean encrypted**: the contents are
plaintext little-endian numeric records (tag pairs like `0B00 E703` repeated, `FFFFFFFF` sentinels,
small counts). `.arr` files are largely zero-filled. `.fin` is 20 bytes with a small count and a
sentinel `0x4416`.

### Where the text still is NOT

`general_archive`, `menu_lang`, `field_lang`, `text/JP` — the paths the EBOOT names — have **0
occurrences in `PACKAGE.BIN`**. Those resources are addressed another way (a different container,
or a second image/section), so following the EBOOT's *names* is not sufficient: the name→data
binding happens somewhere not yet found.

Other small resources checked and ruled out as plain text: `name.bin` (entropy 6.740, no ASCII
runs), `simple_character_select.bin` (14 B), `save_data.bin` (7.495), `common.bin` (6.840),
`battle_voice_name.bin` (6.622 — a u16 table, `0x1001`, `0x1102`... incrementing pairs).


### The index is fully characterised, and it does not point at text

`PACKAGE_INFO.BIN` is EXACTLY a flat array: `16-byte header + 5741 x 12 bytes = 68908 = file size`.

| word | property | interpretation |
|---|---|---|
| `w0` | strictly ascending **5740/5740**, max 4,294,062,453 (just under 2^32) | a monotonic counter/log, **NOT** a byte offset (only 886/5741 land inside PACKAGE.BIN) |
| `w1` | sizes, sum = **1,067,318,536** bytes (1017.9 MiB) | per-entry length |
| `w2` | **in range for 4994/5741 (87%)** vs ~15% by chance | a real offset into PACKAGE.BIN |

`sum(w1) x 4 = 4,269,274,144` against `max(w0) = 4,294,062,453` — within **0.6 %**. Tempting as a
`w0 = 4 x cumsum(w1)` law, it is **false**: tested exactly, it holds for only **1/5741** records. Do
not resurrect it.

Classifying the 64 bytes at every `w2` (`scripts/psp-dissidia-indexclass.py`):

```
binary                2268  39.5%
floats/params         1611  28.1%
zeros/empty           1098  19.1%
OUT OF RANGE           747  13.0%
ASCII text              16   0.3%   <- inspected: repeating bitmap patterns (K8K8]7. ...)
UTF-16LE text            1   0.0%
```

So the index points at **asset data — textures and parameter blocks — not at text.** The "ASCII"
hits are bitmap byte patterns, and their offsets cluster just above `0x20000000` (half the file).

### The final on-disk text search: font codes

Because the menu labels are textures and the game ships a real font (`libfont.prx`,
`SYSTEM FONT T2/T3`, `sceLibFont`), and because `menu_pk_loading_seq_0.bin` holds UTF-16LE with codes
above `0x7F`, the last on-disk hypothesis was **text stored as low-range u16 font codes**.
`scripts/psp-dissidia-fontscan.py` searched the whole payload for long runs of u16 values with a
zero high byte and a bounded code value: **no text runs exist.** The longest matching runs are 23
tokens and are binary data patterns (`\x09\xBF\x06\xCE\x15...`); distinct codes used: 255, dominated
by tiny values (0x01, 0x15, 0x06).

### Conclusion for section 9-10: the text is NOT in PACKAGE.BIN in any plain form

Every plain or semi-plain storage has now been excluded by measurement, over the entire 660 MB file:

* UTF-16LE menu words — 0 hits
* sentence-shaped prose, both encodings — 0 (only font atlases)
* ASCII anywhere — only texture/tool metadata (`title_09_BattleLobby.tm2`, `.kuriki.`, `GimConv 1.41`)
* low-range u16 font codes — 0 text runs
* the `packm` index does not reference text
* the EBOOT's own `text/JP/` paths (`menu_lang.bin`, `field_lang.bin`, `mess_pk_loading`) — **0
  occurrences** in the payload
* `.mes` files — all 11 are empty stubs
* menu labels — baked into `.tm2`/`.gim` textures

Two readings remain, and both now require reading **code**, not data:

1. The localisation text is loaded from a resource **not inside PACKAGE.BIN** (a second archive, a
   section the index addresses differently, or the install data).
2. It is generated at load time by the font/text module from data that is not recognisable as text
   until the decoder runs.

**The next step is therefore unambiguous: read the decoder out of `EBOOT.dec` in the Ghidra project
and stop searching the archive.** Every data-side hypothesis has been tested and falsified; guessing
further has negative expected value.


---

## 9. The `*_help.bin` encoding — unresolved, with the falsifications recorded

`accessory_help.bin` (64682 bytes) is representative:

```
leading u16 table, ascending from u16[0]:
  [6091, 6154, 6229, 6302, 6359, 6416, 6473, 6530, 6587, 6644, 6701, 6758, 6815, 6862, 0, 6917, ...]
entry lengths from consecutive offsets: min 47, max 75, mean 59.3  <- plausible one-sentence sizes
so the container IS a string table: 482 entries, strings begin at 6091
```

The string body (58591 bytes) is **not** any of the obvious encodings. Everything below was
**measured and falsified**, so it does not need retrying:

| hypothesis | measurement | verdict |
|---|---|---|
| UTF-16LE | whole-payload scan for menu words: **0 hits** | ✗ |
| plain ASCII | body printable rate **38.9 %** | ✗ |
| single-byte XOR | best key `0xBF` → 41.1 % printable, 22 % letters | ✗ (near random) |
| substitution cipher | index of coincidence **0.0191** (English ≈ 0.066) | ✗ |
| ECB block cipher | 16-byte blocks: 3503 distinct of 4000 | ✗ |
| simple zlib/gzip | entropy **6.730 b/byte** (compressed ≈ 7.99) | ✗ |
| 6/7-bit packing | 7-bit gives 72 % printable but 38 % letters and ioc 0.0091 | ✗ |
| keystream restarting per entry | entry-pair XOR has the high bit set **49.9 %** of the time (must be 0 %) | ✗ |
| delta / cumulative | entropy 7.99 / 7.82 | ✗ |
| XOR with **random** keystream | would give entropy **≈ 8.0**, but body is **6.730** | ✗ — too low |
| glyph-index stream | all 256 values used, top value only 5.8 %, 189 values above 0.1 % — frequency far too flat | ✗ |

### What it actually looks like: a 16-byte record array, not ciphertext

Splitting the body into interleaved planes (this is the key measurement):

```
even plane: 29296 bytes, entropy 4.254, printable 42.5 %, zeros 8.08 %
odd  plane: 29295 bytes, entropy 7.667, printable 35.2 %, zeros 0.57 %
```

A low-entropy plane beside a high-entropy plane. And the **even plane has a fixed 16-byte template**
with only a few bytes varying per record:

```
e0: 32 2d a6 c8 00 57 f2 4b 36 92 73 84 ed 90 5e 84
e1: 32 2d a6 c8 00 57 f2 4b 36 a2 43 84 ed 90 5e 84
e2: 32 2d a6 c8 00 56 0d 4b 36 92 73 84 ed 90 5e 84
                    ^     ^  ^  ^  ^
```

and the histogram of gaps between zero bytes in the even plane is dominated by **gap 16 (217 times)**,
far above every other gap. Combined with the earlier period-32 autocorrelation (0.342), this says the
body is **16-byte records** (32 bytes in the original interleave), i.e. **structured binary data** —
which is why every text-decoding hypothesis above fails: the premise that this file is a block of
readable prose is probably wrong.

**Two live readings, to be distinguished next:**
1. `*_help.bin` holds **numeric parameter records** (and the human-readable help text is elsewhere —
   e.g. in one of the 1008 undecoded `ARC` containers, or the `text/JP/` resources).
2. It holds text in a **custom per-glyph bit packing** that the 6/7-bit tests did not match.

The reliable discriminator is the decoder itself: find the function in `EBOOT.dec` that opens these
files and read what it does. That is what the Ghidra project is for.

---

## 10. Status

**Done:** disc structure; game ID `ULUS10437`; EBOOT decrypted and the ELF characterised; Ghidra
project imported and analysed (71 s); the `packm` index header; the `MPK ` record layout **verified
two ways**; **all 388 MPK archives decoded and every entry named** (2204 entries / 1747 distinct
names written to a file); the menu resources identified **by name**; the EBOOT's own `text/JP/`
asset paths recovered; 10 concrete encoding hypotheses measured and falsified; the `ARC` format
decoded; **the decoded text located in live RAM** (section 11).

**Not yet done:** reading the menu/help text **through the app**; the `*_help.bin` on-disk encoding
is still unexplained (though it no longer blocks the reader, since RAM holds the decoded form); the
Dissidia **load base is still a placeholder** for the Ghidra import (`0x08804000`).

**Next, in order:**
1. Map the decoded-text region in RAM (`0x09E59xxx`) — how it is grouped, and whether it is stable
   while a menu is open. That is a text-table address for an adapter.
2. Find the transform between the on-disk bytes and the decoded RAM text (the decoder the
   FFVIII-style hook would sit on).
3. Derive the real load base from the live game, then re-scan `EBOOT.dec` for the resource loaders
   (the earlier attempt found 0 of 11 because of the `0x74` file-offset/vaddr skew — see section 7).

---

## 11. BREAKTHROUGH — the decoded text IS in RAM (Dissidia running, ULUS10437)

Loaded the game myself (`C:\Program Files\PPSSPP\PPSSPPWindows64.exe --debugger=12345`) and confirmed
the harness: `game.status` reports `id ULUS10437`, `title DISSIDIA FINAL FANTASY`.

Scanning the **mapped** user-RAM span found real English UI text:

```
@0x09E59124  Other players can see the information on your friend card during wireless play.
             Avoid entering personal information such as addresses, real names, or phone numbers;
             as well as foul, vulgar ...
@0x09E59152  friend card during wireless play. ...
@0x09E59206  Other players can see the names of your artifacts during wireless play. Avoid entering
             personal information ...
```

That is the **friend-card privacy warning** — text belonging to `friend_card.bin`.

### This settles the encoding question

Extracted `friend_card.bin` (4884 bytes) from MPK @1040384 and searched for the same English:

```
friend_card.bin: 0 plaintext hit(s) for 'wireless play'
   UTF-16LE hits: 0
```

**The disk copy contains none of that text in any plain form, while RAM contains it as readable
ASCII.** So the archive copy is genuinely encoded/compressed and the game decodes it at load time.
The on-disk string tables (`*_help.bin`, `name.bin`, `friend_card.bin`) are the *encoded* form; RAM
holds the *decoded* form at **`0x09E59124`**.

This SUPERSEDES the section 9 conclusion that the 16-byte-record structure must be a parameter array:
the same body is text, stored encoded, decoded at runtime. The 16-byte period is a property of the
*encoded* representation, not evidence that no text is present. The measurement that decided it was
searching for a known phrase in both places — disk 0 hits, RAM 2 hits.

### CORRECTION: the address is 0x09E59124, not 0x0AE59124

An earlier revision of this doc recorded the address with a wrong third nibble. The true address is
**`0x09E59124`**. When re-probed, `0x0AE59100` returns
`{"event":"error","message":"Invalid address"}` — it is not mapped at all. The misattribution came
from chunk arithmetic in the older scanner (see the mapped-span note below).

### The readable address space, measured (never assume it)

`scripts/psp-ppsspp-client.py --find ... --region 0x08800000:0x0C000000` probes address by address:

```
readable spans:
   0x08800000 .. 0x0A000000  (24 MiB)
```

Dissidia's user RAM window is **0x08800000–0x0A000000**; reads above it fail. Measured behaviour of a
read crossing the boundary:

```
read @0x09C00000 size=4194304 -> 4194304 bytes   (entirely in range: fine)
read @0x09F00000 size=1048576 -> 1048576 bytes   (in range: fine)
read @0x09FF0000 size=1048576 ->       0 bytes   (crosses 0x0A000000: FAILS WHOLE)
read @0x0A000000 size=4194304 ->       0 bytes
```

**A read past the mapped end fails completely rather than truncating** — which is exactly how the
first naive scanner produced addresses with a wrong prefix: it advanced a chunk counter past the
mapped end and kept attributing later hits to a wrong base.

### Menu labels confirmed NOT to be strings — in RAM as well as on disk

Scanning the readable span for the documented menu names:

```
Customize   0 hits
Ability     0 hits
```

while `friend card` (1), `wireless play` (2) and `Other players` (2) all hit. So the **menu labels
really are textures** (section 4), while genuine string resources exist and ARE decoded in RAM. Disk
and memory now agree.

### Debugger notes (each cost real time)

* PPSSPP emits **asynchronous broadcast events** — observed `input.analog` for both sticks several
  times a second. A client taking the next `ws.recv()` as its reply reads a broadcast instead. **Match
  responses by `event` name and loop until you get your own.**
* An unmapped address answers `{"event":"error","message":"Invalid address","level":2}`. **That is a
  real answer, not noise.** Swallowing it and returning empty bytes cannot distinguish "not mapped"
  from "mapped but zero", which silently corrupts any scan built on it. Surface it.
* The endpoint is `ws://127.0.0.1:12345/debugger`; the root is PPSSPP's file server and replies
  `Handshake status 200 OK`.

### Ghidra correction: import an ELF as an ELF

The first import used a raw `BinaryLoader` at `-loader-baseAddr 0x08804000`, which maps file offset N
to `base + N`. The ELF's first LOAD segment is `file_off 0x74, vaddr 0x00000000`, so every address
was skewed by **0x74** and a scan for the addresses of eleven resource-name strings found **0 matches
across 683,649 instructions** — which reads as "the code never references these strings" when in fact
the addresses were wrong. Re-imported with `ElfLoader` (project `DISSIDIA_ELF`, 115 s analysis), so the
listing now uses the addresses the code actually references.

### Ghidra correction: a ClassNotFoundException IS a compile error

`analyzeHeadless` reports `java.lang.ClassNotFoundException: MyScript` when the Java failed to
compile — Ghidra swallows javac's output. Compile it yourself against Ghidra's jars to see the real
error. That is how this was found: `error: cannot find symbol — method getOpCode()`. Ghidra's
`Instruction` has **no `getOpCode()`**; use `ins.getMnemonicString()`.

---

## 12. The localisation file is NAMED in RAM — and it is `general_archive/main/EN/main_lang.bin`

Section 7 recorded a puzzle: the EBOOT's own asset paths name `general_archive/field/JP/menu_lang.bin`
and `general_archive/field/JP/field_lang.bin`, but **none of those byte strings occur in
`PACKAGE.BIN`** (0 hits for `general_archive`, `menu_lang`, `field_lang`, `text/JP`). The name→data
binding was not found.

It is now found, **in RAM**, while the game is running. A scan for absolute pointers into the
pause-menu text block (`scripts/psp-dissidia-menucursor.py`) found three sites, and dumping around
them shows a resource record holding the path as plain ASCII:

```
0x09EF72D4  09CED310  general_archive/main/EN/main_lang.bin
```

So the English localisation resource is **`general_archive/main/EN/main_lang.bin`** — note `EN`, not
`JP`: the path is language-keyed, and the JP-form strings in the EBOOT are the template.

### But it is NOT on the disc image — an honest negative

Searched the whole 1,646,657,536-byte ISO for `main_lang`, `_lang.bin`, `general_archive` and
`EN/main_lang`: **0 hits for all four.** It is also absent from `PACKAGE.BIN` (0 hits).

So the name in RAM refers to a resource that is either renamed/generated at load time from data under
a different name, or read from a path the emulator resolves outside the image (an install or
memory-stick location). This closes the question: **no further ISO searching will produce a file by
that name**, and the encoded archive text plus the decoded RAM copy (section 11) remain the working
route — which is sufficient for a reader.

### The byte before each menu string is a FLAG, not an index

The pause-menu block at `0x09D16A68` is a real string table. The prefix histogram over 49 entries:

```
0x00 x40   0xFF x6   0x81 x2   0x04 x1
```

`0xFF` appears on conditional/notice lines only (`*EXP and character settings will be retained`,
`*Invokes a penalty of 2 DP`, `You will lose all current progress.`), so it marks
disabled/conditional entries; the rare single-character values are formatting codes. This refines the
section 11 wording ("format prefix") — it is a flag byte, and it is **not** an item index.

### What a reader has, and what is still missing

| what a reader needs | state |
|---|---|
| menu / UI text | **found** — `0x09D16A68`+, UTF-16LE, flag byte per entry |
| system / save / error text | **found** — ASCII, `0x09E58xxx` |
| ability / skill names | **found** — UTF-16LE, `0x09E6Axxx` |
| engine / manager names | **found** — ASCII, `0x08B7xxxx` |
| the localisation source name | **named** — `general_archive/main/EN/main_lang.bin` (absent from the image) |
| **which entry is SELECTED** | **not yet** |

The remaining gap is the cursor. The text block has only **3** inbound absolute pointers, holding
structures of the form `(text_ptr, 0x880, ...)` that look like a resource/manager header rather than a
per-item list — so the selection index is most likely computed in code (the situation Rule 74
describes). Finding it means reading the menu manager (`MENU MANAGER` is a named string in RAM at
`0x08B837FC`), **not** diffing RAM further.

---

## 13. The menu manager is NAMED in RAM — and the text pool is not per-menu

Two more structures found while hunting the selection index.

### The engine registers its own function names

At `0x08B837FC` the engine holds a named-function registry:

```
MENU MANAGER
MENU_MANAGER::ExecuteUpdate
MENU_MAN...
```

alongside the debug names already recorded (`SYSTEM_FONT::Draw`, `VOLATILE_MEMORY_LOADER`,
`SQEXTEC MoviePlayer Display Async`). `MENU_MANAGER::ExecuteUpdate` is the function to read for
cursor behaviour — **but no pointer to the name exists anywhere in readable RAM**: a full scan for
u32 references to `0x08B8380C` and `0x08B837FC` returned **0**. So the registry is consumed by name
lookup (hashed/compared at runtime), not by stored pointers, and the code cannot be reached by chasing
a pointer. The route is Ghidra: find where these names are compared, which is what the `DISSIDIA_ELF`
project is for.

### The text pool at 0x09D16A68 is NOT one menu's item list

The full listing shows strings from **several different screens** interleaved in one block —
`Return to Game` / `Retry` / `Quicksave` (pause menu), `Dark Knight` / `Paladin` / `Normal` /
`EX Mode` (character/mode selection), `Saving replay...` / `Replay saved`, and conditional notices.

So it is a **resident string pool**, not a per-screen item table:

```
0x09D16A68  [0x04] Return to Title Screen
0x09D16A98  [0x00] Return to Start Menu
0x09D16AC4  [0x00] Return to Mode Top
0x09D16AEE  [0x00] Retry
0x09D16AFC  [0x00] Return to Game
0x09D16B48  [0x00] Retry Level
0x09D16B8A  [0x00] Quick Select
0x09D16BBC  [0x00] Forfeit
0x09D16BCE  [0x00] Rematch
0x09D16BE0  [0x00] Flee
0x09D16C12  [0x00] Quicksave
0x09D16C38  [0x00] .Quit Level Progression
0x09D16C6A  [0x00] Forfeit this battle?
0x09D16C9A  [0xFF] *EXP and character settings will be retained
0x09D16CF8  [0x00] Dark Knight
0x09D16D12  [0x00] Paladin
0x09D16D24  [0x00] Normal
0x09D16D34  [0x00] EX Mode
0x09D16D4A  [0x81] Close
0x09D16D70  [0x00] Help Manual
0x09D16D8A  [0x00] Skip Cutscene
0x09D16DA8  [0x00] Resume
0x09D16DB8  [0x00] Save replay
0x09D16DD8  [0xFF] Saving replay...
0x09D16E04  [0xFF] Replay saved
0x09D16E22  [0x00] Continue
0x09D16E48  [0x00] 2Play to next camera data
0x09D16E7C  [0x00] "Camera Edit mode
0x09D16EA2  [0x00] Replay mode
0x09D16EBA  [0x00] (Play from beginning
0x09D16EE4  [0x00] $Reset camera data
0x09D16F0C  [0x00] Save data
0x09D16F20  [0x00] "Return to Museum
0x09D16F84  [0x00] "Return to Museum
0x09D16FAA  [0x00] Quit this battle and return to the level map?
0x09D1700C  [0xFF] *Invokes a penalty of 2 DP
0x09D17044  [0x00] 0Continue to Next Battle
0x09D17078  [0x00] Return to the Arcade Mode selection screen?
0x09D170D6  [0xFF] You will lose all current progress.
0x09D17120  [0x00] .Return to Battle Setup
0x09D17150  [0x00] <Return to Character Selection
0x09D1718E  [0x00]  Return to Lobby
0x09D171B2  [0x00] Return to the title screen?
```

The rare prefixes `[0x04]` and `[0x81]` sit on the first entry and on `Close`, i.e. they are
**per-entry attribute/format codes**, while `[0xFF]` marks notice/conditional lines. Consistent with
section 12.

### The item list IS in RAM, referenced by ID

Immediately before the `main_lang.bin` name string live two node records, one of which holds a
**contiguous ascending list of small integer ids**:

```
0x09CED300  09CED480 09C98280 00000170 00000004
0x09CED310  00000118 00000119 0000011A 0000011B     <- 0x118=280, 0x119=281, 0x11A=282, 0x11B=283
0x09CED320  0000011C 00000000 ...                   <- 284
```

`0x09CED310` is pointed to directly by the resource record at `0x09CED510`
(`09D169B0 09CED530 00000000 0000001C`), and `0x09CED500` in turn is pointed to by the
`main_lang.bin` record. So the chain is:

```
main_lang.bin resource record @0x09EF7244
    -> 0x09CED500   (09CED530 09D1C100 00029D10 00000010)
    -> 0x09CED510   (09D169B0 09CED530 00000000 0000001C)
    -> 0x09CED310 = an ascending ID list (280..284)
    -> 0x09D169B0 = the text pool region
```

**A menu is therefore an ID list plus a text pool**, not an array of text pointers — which is why no
per-item pointer table exists to find. The missing selection index is most likely one of the small
integers in such a node, and reading `MENU_MANAGER::ExecuteUpdate` in Ghidra is the way to confirm
which.

### Session state at the time of writing

The game had stopped changing: sampling the node arena and the text pool 1.5 s apart gave **identical
bytes**, and three separate regions were all static. So the emulator was parked on a static screen
(not in a live menu), and no cursor could be exercised from that state. The cursor work needs the game
actually sitting in a menu.

---

## 14. The game is NOT hung — a button sweep separates "static screen" from "dead game"

Section 13 recorded, from byte-identical RAM samples, that the game "had stopped changing". That
conclusion was **too strong and partly an instrument failure**. Two corrections, both measured.

### `cpu.status.pc` is a stale field in this build — do not use it as an aliveness test

Sampling `cpu.status` returned `pc = 0x08909800` on **every** call, and identically **before and after
a full emulator restart** (`taskkill /F` then a fresh launch). A live PC cannot land on the same
address across a restart, so this field is a placeholder in PPSSPP v1.20.4:

```
cpu.status  {"stepping": false, "paused": false, "pc": 143693824, "ticks": 21225743509}
```

`ticks` does advance steadily (222,222,000 per second), which only says the emulator is executing —
not that the guest game is progressing. **An aliveness test must be the SCREEN or specific RAM, never
`cpu.status.pc`.**

### The decisive test: sweep every button and watch for change

`scripts/psp-dissidia-sweep.py` sends each button in turn and re-signatures the screen plus five RAM
regions after each one:

```
cross     no change        up      no change
circle    no change        down    no change
triangle  no change        left    no change
square    no change        right   no change
start     no change        l       CHANGED: RAM
select    no change        r       CHANGED: RAM
```

**Input reaches the game** — the debugger broadcasts `{"event":"input.buttons","buttons":{"cross":true,...}}`
then `cross:false` on every press, so the injection works. And **`l` and `r` produce a RAM change**
while `cross`/`start`/directions produce none.

So the game is **not hung, and not frozen**: it is sitting on a screen where only the shoulder
buttons act (consistent with a cutscene/scene where `l`/`r` skip or switch), and the earlier
"nothing changes" reading came from sampling regions that this particular screen does not touch.

### The screenshot instrument failed inside the sweep

`sweep.py` reported `img=None` for every button: the in-process `subprocess.run(["python.exe",
"psp-shot.py", ...])` did **not** produce a file, although the same command from the shell works and
writes 197,612 bytes. The environment differs (the sweep runs with `MSYS2_ARG_CONV_EXCL` set and
inherits a different cwd), so the capture must be invoked the way that is known to work — from the
shell, with the export set — and the image signature computed from the file afterwards. **A silent
`None` for a capture is not evidence the screen is static**; it means the capture did not happen. That
is the same class of error as the earlier beacon reading a half-written PNG: verify the instrument.

### What is established, and what is still open

| item | state |
|---|---|
| menu / UI text (UTF-16LE, flag byte per entry) | **found** — `0x09D16A68`+ |
| system / save / error text (ASCII) | **found** — `0x09E58xxx` |
| ability names, engine/manager names | **found** |
| menus are ID lists (`0x09CED310`, ids 280–284) | **found** |
| `MENU MANAGER` / `MENU_MANAGER::ExecuteUpdate` named | **found** (0 pointers — read in Ghidra) |
| the localisation resource name | **found** — `general_archive/main/EN/main_lang.bin` |
| which entry is SELECTED | **still open** |

The cursor remains open, and it needs one thing this session never had: **the game parked in a live
menu with a visible highlight**, so that a D-pad press visibly moves a selection. Sweeping from
whatever screen the game happens to be on cannot find it, as section 14's result shows.



---

## 15. Decompiled: the resource loaders are identified by name

The earlier string-reference scan found 0 references because of the `0x74` file-offset/vaddr skew
(section 11). Re-run correctly against the `DISSIDIA_ELF` project — **searching Ghidra's memory for the
string bytes** rather than trusting precomputed addresses — it found **23 string occurrences and
decompiled 10 referring functions**.

### The loaders

| string | referring function | what it is |
|---|---|---|
| `general_archive` (8 refs) | **`FUN_00122ccc`** | the archive **path builder** |
| `general_archive/field/JP/menu_lang.bin` | `FUN_00122ccc` | the menu-text path template |
| `pause_help.bin` | **`FUN_00124970`** | the **menu resource loader** |
| `accessory_help.bin` | `FUN_001ea728` | accessory help loader |
| `item_help.bin` | `FUN_001f4df4` | item help loader |
| `system.bin` | `FUN_001f06cc` | system data loader |
| `MENU MANAGER` | **`FUN_002489d0`** | menu manager constructor |
| `VOLATILE_MEMORY_LOADER` | `FUN_00104bb8` | memory loader |

### The path templates, verbatim from the code

```
"general_archive/field/JP/menu_lang.bin"
"general_archive/field/menu.bin"
```

Note **`/JP/`** here, while RAM at runtime held `general_archive/main/EN/main_lang.bin`. So the code
carries a **language-template path** and the language component is substituted at runtime — that is
the mechanism section 12 was missing, and it explains why the on-disc image contains no file by the
final name.

### The menu loader's full resource list

`FUN_00124970` names, in one function:

```
pause_help.bin      ap_bonus.bin        dp_bonus.bin        info.bin
command_battle.bin  ptc_auto.bin        ptc_chaos_auto.bin  battle_result.bin
result.bin          voice_load_parameter.bin  battle_voice_name.bin
judgement.bin       super_skill.bin     replay.bin          replay_save.bin
brightness.bin      system_effect.mpk   org99.gmo
battle_dialogue.sequence
```

**This is the menu's complete resource list**, and it is the map for the remaining cursor work.

### `.sequence` is timing data, not text

`battle_dialogue.sequence` (572 bytes, from MPK @631941120 at `data=632420944`) is Director-style
timing records — float values (`0x3F333333` = 0.7, `0x3E99999A` = 0.3) with small integer ids, **no
text**. So the 65 `.sequence` files are animation/timing scripts (matching the `PTC_*` names, which
are battle commands). The UTF-16LE found in `menu_pk_loading_seq_0.bin` (section 9) is incidental
content of that particular file, not the format.

---

## 16. `FUN_002489d0` is the menu manager's constructor — and it holds the selection sentinel

Decompiling the `MENU MANAGER` referrer gives the menu system's whole shape in one function.

### It builds FIVE parallel subsystems, all with the same layout

```c
FUN_003406c4(base, 0x39, size);                        // initialise an array
param_1[N] = param_1[N+1] = param_1[N+2] = arrayptr;   // head / tail / cursor pointers
param_1[N+3] = param_1[N+4] = param_1[N+5] = 0;        // three state fields, zeroed
do {                                                    // TWO PARALLEL pointer arrays
  piVar5[0x10] = (int)piVar7;      // into an N-strided array
  piVar5[0x11] = (int)piVar10;     // into a 2-strided array
  ...advance both...
} while (iVar1 < count);
```

| subsystem | slots | struct stride | array bytes |
|---|---|---|---|
| 1 | 100 (`0x64`) | `0xb` (11 ints) | `0x1458` |
| 2 | 490 (`0x1ea`) | `0x11` (17 ints) | `0x9180` |
| 3 | 71 (`0x47`) | `0x1d` (29 ints) | `0x226c` |
| 4 | 160 (`0xa0`) | `5` ints | `0xc80` |
| 5 | (uses `DAT_00001188`) | — | — |

Each subsystem is **two parallel arrays — a slot array and a pointer array — plus a head/tail/cursor
triple and three state fields**. That is a menu/queue structure, and it is precisely what the earlier
RAM hunts were failing to find by scanning.

### The selection sentinel

Immediately after the first subsystem is set up:

```c
param_1[1] = iVar1;      // allocated sub-object
param_1[8] = -1;         // <-- SELECTION SENTINEL (nothing selected)
param_1[0xb] = 0; param_1[10] = 0; param_1[0xc] = 0;
param_1[0xe] = 0; param_1[0xd] = 0; param_1[0xf] = 0;   // six state fields cleared
```

**`param_1[8] = -1` is the "no selection" initial value**, with six adjacent state fields zeroed at the
same time.

This is the strongest cursor lead in the whole investigation, and it explains the earlier failures:
**the selection is a field of an allocated menu-manager struct** (`param_1`) — exactly the situation
Rule 65 describes, a struct field behind a runtime pointer, invisible to a fixed-address RAM sweep.

### Final callbacks registered

The constructor ends by registering three handlers against a singleton fetched by `FUN_00103c60`:

```c
uVar3 = FUN_00103c60(DAT_00392cd8);
FUN_00103db0(uVar3, 1,          0x88ba,         FUN_0036926c);
FUN_00103db0(uVar3, 0xffffffff, &DAT_00011183, FUN_00369290);
FUN_00103db0(uVar3, 0xffffffff, &DAT_00011189, FUN_003692b4);
```

Those three `FUN_003692xx` callbacks are where per-frame menu behaviour lives — **`FUN_0036926c` runs
on event id 1**, the likely update/execute hook, and the natural partner to the
`MENU_MANAGER::ExecuteUpdate` name found in RAM (section 13).

### The remaining work is small and precise

Read `FUN_0036926c` / `FUN_00369290` / `FUN_003692b4`, and find where the singleton's fields are read.
The cursor is `param_1[8]` (or one of the per-subsystem `[N+3]/[N+4]/[N+5]` triples) resolved through
the pointer returned by `FUN_00103c60(DAT_00392cd8)` — **a live address, obtainable by reading that
global in RAM**, not by scanning.


---

## 17. The menu manager is a STATIC struct driven by an event dispatcher

Three decompile passes walked the callback chain down from the constructor to the real code. Each
level was a stub, and the final level is conclusive.

### The chain

```
FUN_002489d0 (constructor, section 16)
  registers  FUN_0036926c  [event id 1]  ->  FUN_00248dd0(DAT_00397770, p)
             FUN_00369290               ->  FUN_00248dec(DAT_00397770, p)
             FUN_003692b4               ->  FUN_00248ea8(DAT_00397770, p)

FUN_0036926c/90/b4   are 36-byte THUNKS (just forward the global)
FUN_00248dd0         is a one-call stub:  { FUN_0024932c(); }
FUN_00103c60         is a one-liner:      return *(u32*)(param_1 + 0x2000);
```

### `DAT_00397770` is NOT a pointer — it is an inline struct holding a chapter name

```asm
raw: 00 00 00 00 00 00 00 00 6f 6e 65 30 30 00 00 00
                              ^^^^^^^^^^^^^^  "one00"
```

So the menu manager's state is a **static struct** (addressable directly in RAM, no pointer chase
needed), and it carries a chapter id (`one00`) at `+0x8`. `DAT_00392cd8` is 16 zero bytes — a
zero-initialised static, not a vtable.

### The real update is an event dispatcher over two linked lists

`FUN_0024932c` (380 bytes) is `MENU_MANAGER::ExecuteUpdate`:

```c
void FUN_0024932c(int param_1) {
  int iVar3 = *(int *)(param_1 + 0x28);          // head of list A
  if (*(char *)(DAT_00397770 + 0x26) == '\0') {   // a global enable flag
    if (*(char *)(DAT_00397770 + 0x25) == '\0')
      DAT_00397774 = 0x3f800000;                  // = float 1.0
    else
      DAT_00397774 = *(undefined4 *)(DAT_00392d10 + 0x18);
  } else {
    DAT_00397774 = 0;
  }
  while (iVar2 = iVar3, iVar2 != 0) {             // walk list A
    bVar1 = *(byte *)(iVar2 + 0x14);              // node FLAGS
    iVar3 = *(int *)(iVar2 + 0x24);               // node NEXT
    if ((bVar1 & 0xc) == 0) {
      if (*(char *)(iVar2 + 0x17) == '\0') {
        if (((bVar1 & 1) != 0) && ((bVar1 & 2) == 0)) FUN_0025468c();
      } else {
        *(char *)(iVar2 + 0x17) += '\x01';        // per-node COUNTER
        if (2 < *(byte *)(iVar2 + 0x17)) FUN_0024910c(param_1, iVar2);
      }
    }
  }
  /* the SAME loop then runs over list B at param_1 + 0x34 */
}
```

### What this gives the accessibility reader

* **The menu manager is a static struct at `DAT_00397770`** — directly readable in RAM without
  following a pointer. `+0x25`/`+0x26` are enable flags, `+0x8` is the chapter id (`one00`),
  `DAT_00397774` is a float (1.0 / 0 / a config value).
* **Menu items are linked-list NODES**, one record per item, with:
  * `+0x14` — **flags** (bit 0 = active, bit 1 = suppressed, bits 2-3 = a state pair)
  * `+0x17` — a **small counter** (0..2; when it exceeds 2 the item is dispatched)
  * `+0x24` — **next node**
  * `+0x28` in the parent = **head of list A**; `+0x34` = **head of list B**
* **`FUN_0024910c(param_1, node)` is the per-item action** the dispatcher calls on activation.

This is the structure a screen reader wants: walk the list from `DAT_00397770 + 0x28`, read each
node's flags, and the item is actionable when `(flags & 1) && !(flags & 2)`. The **selected** item is
whichever node the game marks with the `+0x17`/flag combination that drives `FUN_0024910c` — the
remaining question, now reduced to reading `FUN_0024910c` and `FUN_0025468c`.

### Honest assessment of what remains

The investigation has narrowed the cursor from "somewhere in 24 MiB" to "one flag/counter field on a
linked-list node rooted at a known static address". That is a large reduction and it is written down.
Closing it fully needs either `FUN_0024910c` read in Ghidra, or the game parked in a live menu so the
list can be walked and correlated with on-screen highlight — and the latter is now cheap, because the
address is static and no scanning is required.



---

## 18. The dispatcher's two callees: node lookup and node removal

Section 17 walked the update function down to its two calls. Both are now read.

### `FUN_0025468c` — per-item DEFINITION LOOKUP (132 bytes)

```c
void FUN_0025468c(int *param_1) {
  iVar3 = param_1[3];                      // a context object
  iVar4 = *param_1;                        // list head
  while (iVar1 = iVar4, iVar1 != 0) {
    iVar4 = *(int *)(iVar1 + 0x3c);        // node NEXT
    if ((*(ushort *)(iVar1 + 0x20) & 1) != 0) {           // node ACTIVE flag (bit 0 of a ushort)
      if ((int)*(short *)(iVar1 + 0xc) < *(int *)(iVar3 + 0x2c)) {   // bounds check!
        iVar2 = *(int *)(iVar3 + 0xc) + *(short *)(iVar1 + 0xc) * 0x1c;   // TABLE INDEX
      } else {
        iVar2 = 0;
      }
      FUN_00254354(iVar1, iVar2);
    }
  }
}
```

So each active node refers to a **definition record in a stride-`0x1c` (28-byte) table**, indexed by
the **short at node `+0xc`**, with the count at context `+0x2c` and the table base at context `+0xc`.
This is the item→resource binding: the `0x1c`-stride table is where an item's own data lives.

### `FUN_0024910c` — node REMOVAL / list repair (544 bytes)

It unlinks a node from the list and repairs links on both sides, then decrements the list count.
Measured field roles:

```c
/* unlink: param_2[10] (+0x28) = PREV, param_2[9] (+0x24) = NEXT */
if (iVar1 == 0) { *(int *)(param_1 + 0x28) = iVar4; }   // head = next
else            { *(int *)(iVar1 + 0x24) = iVar4; }     // prev->next = next
if (iVar4 == 0) { *(int *)(param_1 + 0x2c) = iVar1; }   // tail = prev
else            { *(int *)(iVar4 + 0x28) = iVar1; }     // next->prev = prev
...
*(int *)(param_1 + 0x30) += -1;                          // LIST A COUNT--
```

and the mirror branch for the second list, using `param_1 + 0x34` (head), `+0x38` (tail), `+0x3c`
(count), selected by `(*(byte *)(param_2+5) & 0x10)` — i.e. **node byte `+0x14` bit 4 chooses which of
the two lists the node belongs to**.

It also maintains a free pool (`param_1 + 0x1490` / `+0x1494`) and pushes the removed node back onto
it — so nodes are recycled, which is why they cannot be tracked by fixed address across screens.

### The complete menu-manager field map (measured)

| offset | meaning |
|---|---|
| `+0x8` | chapter id (`"one00"`) |
| `+0x25`, `+0x26` | enable/behaviour flags read by the dispatcher |
| `+0x28` | **list A head** |
| `+0x2c` | **list A tail** |
| `+0x30` | **list A count** |
| `+0x34` | **list B head** |
| `+0x38` | **list B tail** |
| `+0x3c` | **list B count** |
| `+0xa618`, `+0xa61c`, `+0xa620` | a separately-maintained queue (head/tail/count) |
| `+0x1490`, `+0x1494` | **free-node pool** (head / write pointer) |

Per-NODE fields:

| offset | meaning |
|---|---|
| `+0xc` | **short: index into the stride-`0x1c` definition table** |
| `+0x14` | byte flags — bits 0/1 = actionable, bit 4 selects list A or B, bits 2-3 a state pair |
| `+0x17` | byte counter (0..2, then the item is dispatched) |
| `+0x20` | ushort — bit 0 = **active** |
| `+0x24`, `+0x28` | **next / prev** (doubly linked) |
| `+0x3c` | **next** (the singly-linked walk order) |
| `+0x9` (`param_2[9]`), `+0x10` (`param_2[10]`) | next / prev for the unlink path |

### Honest conclusion on the cursor

This is a **node/allocation system**, not a cursor field. The selection is tracked by the menu code
that walks these lists, and the dispatcher's job is lifecycle (activate, count, retire, recycle) —
**not** "which row is highlighted". The `+0x17` counter is a **debounce/confirm timer** (0..2 then
dispatch), which is why it looked cursor-like and is not.

So after four decompile passes the honest result is:

* the **menu state is a static struct at `DAT_00397770`** — no pointer chase, directly readable;
* **items are nodes in two linked lists** with an explicit active flag and an index into a
  definition table;
* the **selection index is not one of these fields** — it lives in the menu screen code above this
  layer, which has not been located.

That is a real narrowing (from "somewhere in 24 MiB" to "one layer above a known static struct"), but
it is **not solved**, and no further RAM scanning will solve it. The next honest step is to find the
code that *reads* `DAT_00397770 + 0x28` and compares against a stored value — a `getReferencesTo`
query on the static plus a search of its readers — or to observe the lists live in a menu with the
node counter and flags recorded per row.



---

## 19. The load base is DERIVED and CONFIRMED: 0x08804000

Every section from 7 onward carried the caveat "the Dissidia load base is still a placeholder
(`0x08804000` was used for the Ghidra import) — it must be derived from the live game before any RAM
address is trusted." **That is now resolved.**

### Derivation (two independent measurements of the same bytes)

The `DISSIDIA_ELF` project (ElfLoader) reports `MENU MANAGER` at Ghidra address `0x0037F7FC`.
The live game's RAM scan found the same string at `0x08B837FC`.

```
RAM address    0x08B837FC
Ghidra address 0x0037F7FC
difference     0x08804000   <-- the load base
```

Because the ELF was imported **as an ELF**, Ghidra addresses are `vaddr`, so this one subtraction
gives the base directly. (This is exactly why the earlier `BinaryLoader` import was unusable: it
mapped offset+N, adding a `0x74` skew on top, which made even the correct base look wrong.)

### Confirmation against the live game

Four independent strings, each computed as `base + ghidra_addr` and read back from the running game:

| Ghidra addr | expected string | live RAM | result |
|---|---|---|---|
| `0x0037F7FC` | `MENU MANAGER` | `0x08B837FC` | **FOUND** |
| `0x00372E20` | `pause_help.bin` | `0x08B76E20` | **FOUND** |
| `0x00372E60` | `MENU_MANAGER::ExecuteUpdate` | `0x08B8380C` | **FOUND** |
| `0x003788E4` | `item_help.bin` | `0x08B7C8E4` | **FOUND** |

**Conversion rule for this game: `RAM = 0x08804000 + ghidra_address`.**

### Why this matters for the remaining work

The open question (which menu item is highlighted) previously required locating an *allocated struct*
behind a runtime pointer. Section 17 established the menu manager is instead a **static struct at
`DAT_00397770`**, whose live address is now computable:

```
DAT_00397770 (Ghidra)  ->  RAM 0x08B9B770
```

with its item lists at `+0x28` (head), `+0x2c` (tail), `+0x30` (count) and `+0x34`/`+0x38`/`+0x3c`
for the second list. So the live menu state is now **directly readable at known addresses** — no
scanning, no pointer chase. That is the whole prerequisite for the cursor step, and it is complete.

**Caveat retained:** the base is confirmed for *this* build (ULUS10437, disc v1.00). The project rule
stands — never carry a base across games.



---

## 20. CORRECTION to section 17: `DAT_00397770` is NOT the menu manager

Section 17 stated that the menu manager is a static struct at `DAT_00397770`. **That is wrong**, and
reading the live game proves it.

### What `DAT_00397770` actually is: a chapter/stage name table

With the load base now confirmed (section 19), the struct was read live at RAM `0x08B9B770`:

```
+0x007  'one00'      +0x02C  'two00'      +0x050  'thr00'      +0x074  'for00'
+0x098  'fiv00'      +0x0BC  'six00'      +0x0E0  'sev00'      +0x104  'eht00'
+0x128  'nin00'      +0x14C  'ten00'      +0x170  'org50'      +0x194  'org00'
+0x1B8  'one01_easy' +0x1DC  'two01_easy'
```

**Stride 36 bytes (0x24), 14+ sequential entries.** These are story-chapter / stage identifiers, not
menu list pointers. The fields section 17 labelled `listA tail +0x2c` and `listA count +0x30` are in
fact the characters `"two0"` and `"0"` — i.e. the next entry of the name table.

### The mistake, precisely

`FUN_00248dd0(DAT_00397770, param_1)` — I read the first argument as "the manager" and built the
section-17 field map from it. **`DAT_00397770` is the ARGUMENT passed in; `param_1` is the object.**
The dispatcher's own code made this visible all along:

```c
void FUN_0024932c(int param_1) {
  iVar3 = *(int *)(param_1 + 0x28);     // list inside param_1, NOT inside DAT_00397770
```

So the `+0x28`/`+0x2c`/`+0x30`/`+0x34` list fields belong to `param_1`, and the chapter table was
being misread as those fields because I dumped the wrong object.

### The real manager is reached through the singleton

`FUN_00103c60` is `return *(u32*)(p + 0x2000);` and was called with `DAT_00392cd8`. Reading live:

```
DAT_00392cd8 (RAM 0x08B96CD8) = 0x08C5DC80   <-- a real pointer
```

and `0x08C5DC80` is a genuine allocated structure with an internal pointer network:

```
+0x000  08C5DCB0 08C5FC80 000008C8 00000004
+0x010  08C5EBF0 08C5DCB0 00000000 0000001C
+0x020  00000004 08BA4468 00000000 00000000
+0x030  08C5DCD8 08C5DC94 00000028 00000004
+0x040  08C5DC80 08C5DE58 08C5E304 00000018
+0x050  08C5DCE8 08C5E304 08C5DD34 08C5DCB0
```

Its records cluster in `0x08C5DC80`–`0x08C5FC80` — a heap region, i.e. **allocated at runtime**, which
is consistent with section 16's `param_1[8] = -1` selection sentinel being a field of an allocated
struct (the Rule 65 situation). That remains the live hypothesis.

### Status after this correction

| claim | state |
|---|---|
| load base `0x08804000` | **confirmed** (section 19, four strings) |
| `DAT_00397770` is the menu manager | **RETRACTED** — it is a chapter/stage name table (stride 36) |
| menu items are linked lists inside the manager | **unverified** — the field map was built from the wrong object |
| a manager object exists at `*(DAT_00392cd8)` = heap `0x08C5DC80` | **read live**, pointer network present |
| which item is highlighted | **still open** |

**Lesson:** before building a field map from a global, check **how the global is used in the call** —
argument vs object. `FUN_00248dd0(DAT_00397770, param_1)` names both, and reading the argument as the
subject inverted the entire structure. A live byte-dump then caught it immediately, which is the
argument for reading the live value of a global before theorising about its layout.



---

## 21. The boot sequence names the localisation files and their load layout

Following the constructor's caller chain to its root found the game's init function `FUN_002dbf50`
(1192 bytes), which is the whole boot path. Two findings, both decisive.

### The real localisation filenames, and where they are loaded

```c
iVar2 = FUN_00103aac();                                                    // a 2 MiB work buffer
iVar3 = FUN_000e8dc0(PTR_s_general_archive_main_main_bin_0039b300);        // size of main.bin
iVar4 = FUN_000e8dc0(PTR_s_general_archive_main_JP_main_lan_0039b304);     // size of main_lang.bin
iVar5 = FUN_000e5710(iVar2, 0x200000, 0x40, 0);                            // allocate 2 MiB
iVar8 = iVar5 + 0x180000;                                                  // language at +1.5 MiB
iVar6 = FUN_000e8bc8(PTR_s_general_archive_main_main_bin_0039b300, iVar5, 0x180000, 1);   // load 1.5 MiB
iVar3 = FUN_000e8bc8(PTR_s_general_archive_main_JP_main_lan_0039b304, iVar8, 0x80000, 1); // load 512 KiB
```

So the paths are, verbatim from the code:

```
general_archive/main/main.bin          <- base data,  loaded to buffer+0x000000
general_archive/main/JP/main_lang.bin  <- LANGUAGE,   loaded to buffer+0x180000 (512 KiB)
```

**`main.bin` is the base and `main_lang.bin` is the language overlay**, both read into one 2 MiB
buffer with their sizes checked before loading (the loads are guarded — `iVar6 == iVar3` etc. — and
the whole block only runs if both lookups succeed).

This explains the whole of section 12: the runtime path seen in RAM was
`general_archive/main/**EN**/main_lang.bin` while the code carries `/**JP**/`. **The language
component is a variable substituted into that template** — the code shown here is the JP branch.

### `FUN_002489d0` is called with ONE argument

```
FUN_002489d0(DAT_00397770);
FUN_0024adf8(DAT_00397770, auStack_30);
```

The constructor's decompiled signature is `undefined4 FUN_002489d0(int *param_1)`. Called as
`FUN_002489d0(DAT_00397770)`, that means **`param_1 = DAT_00397770`** — the chapter/stage name table
address from section 20 **is** the object the constructor builds into. It is not a separate manager.

So the correct reading of section 16's output is: the fields the constructor writes
(`param_1[8] = -1`, the five subsystem triples, etc.) live **in the same static region as the chapter
name table**, starting at `DAT_00397770`. Section 17 built its field map on that same object and so
was not wrong about the address; section 20 was right that the first bytes are a chapter table and
wrong to conclude the object was something else entirely. Both readings describe one static struct
that begins with a chapter-name array.

### The boot order around it

```
... FUN_001f31f4(); FUN_000fce50(); FUN_000fd318();
FUN_00107348(auStack_2c);
FUN_002489d0(DAT_00397770);        <- MENU MANAGER constructor
FUN_00269758(DAT_00398358, auStack_2c);
... FUN_001f06cc(auStack_30, auStack_2c);   <- the system.bin loader identified in section 15
FUN_001f8d10(auStack_30, auStack_2c);
FUN_0024adf8(DAT_00397770, auStack_30);     <- a SECOND call on the same object
```

`FUN_0024adf8(DAT_00397770, auStack_30)` is the natural next target: it takes the object **and** a
context, and runs after the menu manager is built.

### Status

| question | state |
|---|---|
| real localisation filenames | **found**: `general_archive/main/main.bin` + `.../JP/main_lang.bin` |
| how they load | **found**: one 2 MiB buffer, language at `+0x180000`, 512 KiB, guarded loads |
| why `/EN/` appears at runtime but `/JP/` in code | **answered**: language token substituted into a template |
| what object holds the menu state | **`DAT_00397770`** (constructor called with it as the only arg) |
| which field is the selection | **still open** — next: `FUN_0024adf8` and the `param_1[8]` sentinel |



---

## 22. `FUN_0024adf8` is the menu SOUND loader — and the object is system-wide

Section 21 named `FUN_0024adf8(DAT_00397770, auStack_30)` as "the natural next target for the
selection field". **Read, it is not that.** It is 152 bytes and loads menu audio:

```c
void FUN_0024adf8(int param_1, int param_2) {
  if (*(int *)(param_1 + 0x20) == -1) {                      // -1 == "not loaded yet"
    if (param_2 == 0) {
      uVar1 = FUN_0019f8f8(DAT_00394400, "sound/snd_menu.scd");
      *(undefined4 *)(param_1 + 0x20) = uVar1;
    } else {
      iVar2 = FUN_000ef2d0(param_2, "snd_menu.scd");
      if (iVar2 != 0) {
        uVar1 = FUN_0019f924(DAT_00394400, "sound/snd_menu.scd",
                             *(undefined4 *)(iVar2 + 4), *(undefined4 *)(iVar2 + 8));
        *(undefined4 *)(param_1 + 0x20) = uVar1;
      }
    }
  }
}
```

So:

* the "context" argument (`auStack_30`) is an **archive handle** the sound file is fetched from — not a
  menu context;
* `param_1 + 0x20` is a **sound-handle cache slot**, using `-1` as "not loaded", which is the *same
  sentinel idiom* the constructor uses at `param_1[8] = -1` (section 16) — useful to know, and a warning
  that a `-1` in this struct does not imply "selection" either;
* the caller set is **four functions** (`FUN_0012445c`, `FUN_0019cd38`, `FUN_001cdc80`, and boot's
  `FUN_002dbf50`), i.e. this "menu manager" object is used by several subsystems, not just menus.

### What that means for the cursor hunt

`DAT_00397770` is **not a menu object**. It is a system object that the menu manager constructor
initialises along with sound, archive and other subsystems. Confirmed structure so far:

| field | meaning | evidence |
|---|---|---|
| `+0x020` | menu-sound handle cache (`-1` = unloaded) | `FUN_0024adf8` |
| chapter-name array | story/stage ids, 36-byte stride, from offset ~`+0x08` | section 20 live read |
| `param_1[8]` = `+0x20` | **the same field** — so section 16's "selection sentinel" was actually this sound slot | reconciling 16 with 22 |

⛔ **That last row matters.** `param_1[8]` in the constructor and `param_1 + 0x20` here are the *same
4-byte slot* (index 8 = 8×4 = 0x20). So the constructor's `param_1[8] = -1` sets the sound handle to
"unloaded" — it was **never a selection sentinel**. Section 16's claim is therefore **retracted**.

### Status, stated plainly

| claim | state |
|---|---|
| `FUN_002489d0(DAT_00397770)` builds the menu subsystem data | holds |
| `param_1[8] = -1` is a **selection** sentinel | **RETRACTED** — it is `+0x20`, the sound handle |
| menu items are linked lists in this object | **unverified** — the field map that suggested it came partly from this same misread |
| which field holds the selection | **OPEN**, and this object is not obviously the place |

**Where this leaves the investigation:** five decompile passes have mapped the boot sequence, the
resource loaders, the localisation template, and this system object — but the object that holds
*menu selection* has not been identified, and the fields previously nominated for it are now explained
as other things. The honest next step is **not** another guess at offsets: it is to drive the game to
a menu and read the object live while stepping the highlight (the approach section 14's sweep showed is
possible once a live menu exists), or to locate the menu *screen* code rather than the menu *manager*.



---

## 23. Confirmed: the localisation text is NOT a member of any archive — it is resolved by a FILE SYSTEM

Section 21 found the boot function loading two paths. This section confirms the strings **exactly**, by
resolving the pointers independently in the ELF, and then establishes where those files are *not*.

### The two paths, resolved from the ELF

`FUN_002dbf50` calls `FUN_000e8dc0(PTR_s_general_archive_main_main_bin_0039b300)` — a **pointer**, not
an inline string. Following it through the ELF's own load segments:

```
vaddr 0x0039B300 -> file 0x39B374 -> contents 0x003895BC -> "general_archive/main/main.bin"
vaddr 0x0039B304 -> file 0x39B378 -> contents 0x003895DC -> "general_archive/main/JP/main_lang.bin"
```

Both confirmed **verbatim**. So the localisation loader opens, precisely:

```
general_archive/main/main.bin          <- base data      (1.5 MiB, to buffer+0x000000)
general_archive/main/JP/main_lang.bin  <- LANGUAGE text  (512 KiB, to buffer+0x180000)
```

### Neither name is in the archive

| where searched | `main.bin` | `main_lang.bin` | `general_archive` |
|---|---|---|---|
| all 388 MPK archive manifests (`all-names.txt`) | **0** | **0** | **0** |
| `PACKAGE.BIN` (660 MB, byte search) | **0** | **0** | **0** |
| the whole ISO (1.65 GB, byte search) | **0** | **0** | **0** |

The archive names are things like `battle.bin`, `system.bin`, `item.bin`, `pause_help.bin`. There is no
`general_archive` directory and no `main.bin` entry anywhere on the disc.

### Therefore the localisation text is not in a file whose name appears on the disc

This is a **positive conclusion, not a failure**. Three independent facts now agree:

1. the code requests `general_archive/main/...` paths that contain **no bytes present anywhere on the disc**;
2. the archive's own 1747 entry names contain **no matching member**;
3. RAM at runtime held the substituted form `general_archive/main/**EN**/main_lang.bin` (section 12),
   i.e. a path with a language component the disc does not spell out either.

So `FUN_000e8dc0` is **not** an archive-member lookup. It is a **file-system service** that resolves a
virtual path — and the name→data binding happens *inside that service*, not in the archive index. That is
exactly why grep-ing the disc for the name has now returned 0 four separate times (sections 7, 12, 21, 23).

### Where the text physically is

Given the loader reads 1.5 MiB + 512 KiB into a 2 MiB buffer, and the decoded menu text appears in RAM
around `0x09D16A68` / `0x09E58xxx`, the practical consequences for a reader are unchanged and good:

* the **decoded text is in RAM** and readable (section 11) — this is what a reader needs;
* the **encoded source** is reached through a virtual file system, so reproducing the decode offline
  requires reading that service (function `FUN_000e8dc0` and its siblings), not the archive;
* searching the disc for the resource by name is now **formally exhausted** — four attempts, all 0, with
  the cause identified. Do not retry it.

### Stop-condition recorded

**Do not grep the disc image for `main_lang` / `general_archive` / the EN path again.** It has been done
in four sections with four zeros, and section 23 explains why. The remaining routes are (a) the virtual
file-system service, or (b) the decoded RAM text, which is already located.



---

## 24. The live-menu cursor test: a clean NEGATIVE on `DAT_00392cd8`

Sections 16-23 left the cursor open and said the next step was a live menu. **That step has now been
taken** — the game was driven to a menu and the candidate object polled while stepping it.

### Reaching a live menu (previously the blocker)

A button sweep with screenshots captured **from the shell** (in-process capture silently fails, Rule 89)
showed the screen genuinely changing:

| button | mean lum | saturated % | signature |
|---|---|---|---|
| cross | 215 | 0.0 | `e8eea0ea` |
| circle | 209 | **8.3** | `e9cf018f` CHANGED |
| triangle | 209 | **8.3** | `72d81281` CHANGED |
| start | 236 | 0.0 | `22d041c4` CHANGED |
| select | 203 | **10.9** | `67b67381` CHANGED |
| l | 203 | 10.7 | `daeaa939` CHANGED |
| r / down / up | 203 | 10.7 | `daeaa939` (no change) |

Colour appears only when `circle`/`triangle`/`select` are pressed — i.e. **those opened UI**. The screen
then measured a two-frame difference of **0.000% with 10.7% saturated pixels**: a **static menu**, exactly
the condition the cursor work required. This is the first time the investigation had that condition.

### The test

With the screen confirmed static, `DAT_00392cd8` was read, then `down` and `up` pressed alternately,
re-reading the whole 0x40 window each time:

```
+0x00 = 0x08C5DC80        +0x20 = 290 (0x122)
+0x08 = 0x08C5DCE8        +0x30 = 0x09D38400
+0x10 = 0x09EF73A8        +0x38 = 0x08BF8D60
+0x18 = 0x09D36510
+0x1C = 0x09D36510
```

### Result: NEGATIVE, and unambiguous

| field | behaviour across base/down/up/down/up | verdict |
|---|---|---|
| **`+0x20` = 290** | **never changed** | **not the cursor** |
| `+0x08` | changed once, in a heap range (`0x08C5...`) | **allocation churn** |
| everything else | constant | — |

So `0x122` (290) looked like a menu index — a plausible value in a plausible object — and **is not
the selection**.

Compounding it: `down` produced a screen change only once in three rounds, so **this particular screen
does not scroll with `down`**. Whatever drives it is a different control, and the cursor test was run
against the wrong input as well as the wrong object.

### What this settles, and what it does not

**Settled:** the "menu manager object" hypothesis chain (sections 16, 17, 21) does not lead to the
selection. `DAT_00392cd8` holds system-service pointers and a constant 290; it is not a menu cursor
container. Section 22's retraction of the `param_1[8]` sentinel was correct, and this closes the line.

**Not settled:** which field, in which object, holds the highlight. But the *method* is now proven
end-to-end — reach a static screen, poll candidates, press the actual scrolling control — and it needs
only the correct control (try `l`/`r`, which did change the screen, and the d-pad on a screen that
actually has a list) and a candidate set built from the menu *screen* code rather than the manager.

### Method notes worth keeping

* **Saturated-colour percentage is a good "is this UI?" detector**: 0.0 % on a scene, 8-11 % once UI
  appears. Cheaper and more decisive than trying to read text in a screenshot.
* **Screen signature alone conflates animation with navigation.** A live scene changes constantly; a
  menu is static. Measure the two-frame difference *first*, then interpret presses — otherwise a press
  on an animating screen looks like progress (Rule 88's lesson, now with the measurement attached).



---

## 25. Driving the game: OCR, the prologue wall, and an honest status on Story Mode

The task was to get into Story Mode. **Not reached**, and this section records exactly where the
attempt stopped, because the blockers are real and repeatable rather than incidental.

### A new instrument: OCR works, and it reads the plain UI font

`scoop install tesseract` + `scoop install tesseract-languages`. Note the packaging trap:
**`TESSDATA_PREFIX` must point at the `tesseract-languages` pack**, because scoops's `tesseract`
ships no `eng.traineddata` of its own (the default path fails with
`Error opening data file .../tesseract/current/tessdata/eng.traineddata`).

Reading the screen as text is the right instrument for an accessibility project and it immediately
paid off on the **plain UI font**:

| screen | OCR read |
|---|---|
| pause | `PAUSED` … `Return to Game`, `Retry` |
| tutorial | "Bravery attacks: use © to steal bravery!" then "HP attacks: use @ to deal damage!" |
| save/title | `Load` … `DISSIDIA FINAL FANTASY` … `GAME DATA` |

But it **cannot** read the stylized display font: on a scene it returns noise (`—EE`, `-™`, `Se`).
So OCR is a good reader for menus in the plain font and useless for scene text — RAM text remains the
general answer. Script `scripts/psp-ocr.py` wraps capture + preprocessing + tesseract.

### What the input sweep established

Driving with blind presses, the tutorial advanced in a repeatable, meaningful order:

1. `Bravery attacks: use © to steal bravery!` — waits for **circle**
2. `HP attacks: use @ to deal damage!` — waits for **square**
3. `A @ attack can wm it!` — the finisher

This is why the first `cross` sweep looked like a hang: **`cross` is not the button these tutorial
prompts want.** A press that produces no change on a screen whose prompt names a different button is
not evidence the game is stuck.

### Where it stopped

After finishing that scripted sequence the game returned to an animating scene, and a save
(`ULUS10437GameData00/DISSIDIA.BIN`, 286,344 B) was written. The save **loads into the prologue
tutorial battle**, not into Story Mode, so "Continue" does not lead there.

Story Mode sits on the **title screen's** mode list (Story / Battle / Customize / Museum / Shop /
Options / Data). Reaching Story Mode therefore requires either finishing the prologue or navigating
the title menu by blind input, and the title menu's labels are in the stylized font that OCR cannot
resolve — so the navigation loop is blind again. **That is the wall.**

### Corrected in this section

An earlier claim in this turn — "story mode is loaded, the region asset strings are resident" — was
**wrong**, and the check that falsified it is worth keeping: every one of those strings
(`region/storypoint.stp`, `talkevent/%s`, `region/map/st_%02d_%02d.rgn`,
`region/prog/progress_%02d.prg`, `region/reward/DPB_get_tbl.dgt`) **is present in `EBOOT.BIN.dec`
itself**. They are static EBOOT data and are resident in RAM whenever the module is loaded,
regardless of game state. This is the **fourth instance of the same trap** (resident string pool ≠
live state; menus are ID lists; chapter names are static; `main_lang.bin` is a template), and the
general rule is now explicit:

> **Before treating a resident string as evidence of game state, check whether the string exists in
> the EBOOT.** If it does, its presence carries no state information at all. The only strings that
> indicate live state are ones that are *absent* from the ELF and present in RAM (e.g. the
> `main_lang.bin` menu labels), and even those are loaded once at startup rather than per-screen.

### Method corrections to carry forward

* **Screenshot capture from an in-process `subprocess.run()` silently produces no file**; capture
  works from the shell. This has now bitten three separate scripts, so `psp-ocr.py` reports
  `CAPTURE FAILED` explicitly rather than returning "no change" (a missing capture must never read as
  a negative result).
* **`taskkill //F` fails under MSYS** ("Invalid argument/option - '//F'") and `cmd /c start` without
  care spawns duplicate emulator instances — two and then three PPSSPP processes were left running.
  Kill with PowerShell `Stop-Process -Force` and verify the count is exactly 1 before driving.
* **A press that changes nothing is not proof of a hang** — check which button the on-screen prompt
  names first.



---

## 26. Root cause of the silent capture failure: the child interpreter has no PIL

Section 25 recorded that screenshot capture worked from the shell but produced **no file** when
invoked in-process, and that this had broken three scripts. That is now **fixed at the root**, not
worked around.

### The diagnosis

Reproducing both paths side by side and printing the child's stderr gave the cause immediately:

```
parent sys.executable: C:\Users\Devin Prater\AppData\Local\hermes\hermes-agent\venv\Scripts\python.exe
rc: 1
stderr: ModuleNotFoundError: No module named 'PIL'
```

`psp-shot.py` invoked `python.exe` by name. **A bare `python.exe` resolves to whichever interpreter
the parent's PATH puts first**, and when the parent is Hermes's own venv that interpreter has no PIL.
The child therefore died before writing anything — and because the caller only checked *whether a file
appeared*, a crashed capture was indistinguishable from "the screen did not change".

So the failure was never about window handles, permissions or timing. It was **an implicit
interpreter dependency plus a caller that could not tell "no result" from "error"**.

### The fix

`psp-shot.py` now encodes the PNG itself with `struct` + `zlib` (stdlib only), falling back to that
writer whenever PIL is unavailable, and reports which path it used:

```
via PIL      (shell: Hermes venv not first on PATH)
via stdlib   (in-process: child runs under the Hermes venv)
```

Verified both ways: in-process capture now returns `rc: 0`, a 1,678,748-byte PNG that decodes to a
real 1706x1066 RGB image with **45,969 unique colours** (not a blank frame).

### What it unblocks

The repaired `psp-reach-menu.py` then ran correctly for the first time and produced real measurements
instead of nine `CAPTURE FAILED` lines:

```
start: (82.665, 4.04)
  cross     diff=60.431  sat=2.72   animating
  circle    diff=58.786  sat=5.64   animating
  triangle  diff=67.629  sat=5.69   animating
  square    diff=86.062  sat=3.38   animating
  select    diff=76.664  sat=1.5    animating
  start     diff=0.0     sat=5.68   STATIC+UI  <-- MENU

REACHED a static UI screen via 'start' -- run the cursor test now.
```

It found the static UI screen by itself and named the button that produced it. That is the loop
section 24 had to do by hand.

Two further defects surfaced and were fixed while verifying:

* `psp-reach-menu.py` crashed on `"start: %s" % (m if m else ...)` — a **tuple** formatted with `%`,
  which raises `TypeError: not all arguments converted`. Now `% (m,)`.
* `psp-menu-cursor2.py` raised `IndexError` when a read came back short; it now compares only the
  width every sample actually has, and says so.

### The cursor test on the PAUSED menu: still negative, and now for a known reason

With capture working, the cursor test ran cleanly on the pause menu with its own list control
(`down`/`up`):

```
=== fields that moved ===
   +0x08  147184872 -> 147185240 -> 147185240 -> 166470912 -> 147185240 -> 147184872 -> 147184872
```

Only `+0x08` moves, and it moves in a **heap range** (`0x08C5...`/`0x09D...`): that is allocation
churn, not a selection. `DAT_00392cd8` remains **not** the cursor container — consistent with section
24, now confirmed on a second, different menu.

### Rule

> **A subprocess that dies is not a negative result — check the child's stderr and exit code.**
> Invoking an interpreter by bare name (`python`, `python.exe`) inherits the parent's PATH, so the
> child may run under a different interpreter with a different package set. Report capture failure
> explicitly (`CAPTURE FAILED`) and never let "no file appeared" stand in for "the value did not
> change" — a broken instrument and a true zero look identical otherwise.



---

## 27. The registered handlers are DRAW CALLBACKS — the manager layer is closed

Section 26 left the impression that the constructor's three registered handlers were "the menu screen
code" that sections 24/25 said was missing. **That is now tested and it is wrong.** This section
records the closure, because it is the difference between a live lead and a spent one.

### What the handlers actually are

`DisMenuHandlers.java` decompiled all three plus the registration function and its context:

```
FUN_0036926c(undefined4 param_1)        size=36
{
  FUN_00248dd0(DAT_00397770, param_1);   // one-call thunk
  return;
}
```

```
FUN_00103c60(int param_1)               size=8
{
  return *(undefined4 *)(param_1 + 0x2000);
}
```

```
FUN_0024aee0(int param_1, int *param_2) size=4776   // <-- the shared callee
```

Three facts together settle it:

1. **The handlers are one-call thunks** into `FUN_00248dd0` / `FUN_00248dec` / `FUN_00248ea8`, which are
   event bodies guarded on `*(char *)(param_1 + 0x24) != '\0'` and a pointer at `+0x28` or `+0x34`,
   then fire a fixed sequence of `FUN_00351c8c(9)`, `FUN_00351c8c(4)`, `FUN_0035292c(6,0,0xff)` …
   and finally `FUN_0024aee0(this, this+0x28)`. That sequence is a **render/state-swap sequence**.
2. **`FUN_0024aee0` is 4,776 bytes** with dozens of float locals and buffer pointers. It is a
   **renderer**, not a selection field. No 4.7 KB function is a menu index.
3. **`FUN_00103c60` is an 8-byte accessor** — `return *(undefined4 *)(param_1 + 0x2000)` — and it has
   **28 call sites** across the binary (`0x000b29f8` … `0x002d586c`). A function this widely used is a
   **global engine handle**, not a menu object.

So the handlers are **event/draw callbacks** registered on a global engine service. They are not button
handling and they do not contain the highlight.

### What this closes

Combined with the earlier results, the "menu manager" line of enquiry is now exhausted from both ends:

| layer | what it turned out to be | verdict |
|---|---|---|
| `DAT_00397770` (constructor arg) | static struct starting with chapter-name data (`one00`, `two00`, …) | **not** menu state (section 20/21) |
| `param_1[8] = -1` | byte offset `+0x20` = the **menu-sound handle** being set to "unloaded" | **not** a sentinel (section 22) |
| `DAT_00392cd8 + 0x20 = 290` | **never moved** across down/up on two different menus | **not** the cursor (sections 24, 26) |
| the three registered handlers | one-call thunks → fixed render sequence over a 4,776-byte renderer | **draw callbacks** (this section) |
| `FUN_00103c60` | 8-byte accessor `*(param_1 + 0x2000)`, 28 call sites | global engine service |

**The cursor is not in the manager layer at all.** Every candidate field in it now has a different,
verified explanation, which is a stronger result than "not found": the search space is genuinely
reduced rather than merely unsearched.

### The lead that follows

The 179-string UI dump localises what is needed. The strings are **contiguous in address order** in
`0x09D16A68..0x09D1943C` — `Return to Title Screen`, `Retry`, `Quicksave`, `Return to the Lobby`,
`Help Manual`, `Skip Cutscene`, the Story Mode help line at `0x09D18994` — which means **menu entries
are indexed into that table**. So navigation state is most likely an **index into a table of records
that contain these strings**, and the tables themselves are the unsearched thing.

Two concrete, bounded next steps:

* **Find what references the UI text table** (search Ghidra's memory for the bytes at `0x09D16A68`, the
  technique that worked in section 13 after the address-computation approach failed) and read the
  neighbouring records for an index/count pair.
* **Locate the mode-selection screen** (`"return to the mode selection screen"`, `0x09D191DC`) rather
  than the manager — the screen code is where a highlight is drawn from an index.

### Method note

> **A "size" reading is a strong, cheap classifier.** A 36-byte function beside a 4,776-byte one is a
> thunk beside a renderer; an 8-byte function with 28 call sites is an accessor, not a container.
> Reading sizes before decompiling bodies would have prevented treating these handlers as the menu
> screen code at all.



---

## 28. The "pointers into the text table" hypothesis is FALSIFIED (alignment test)

Section 27 closed with a stated lead: menu entries are contiguous in address order, therefore
navigation state is "most likely an **index into a table of records** holding those strings", and the
follow-up was to find what references the UI text table. **That lead has now been tested and it is
wrong.** This section records the test because it retires a plausible-sounding idea cheaply.

### The test, and why it is conclusive

A full readable-RAM scan for any 4-byte value landing in the UI text range `0x09D16A00..0x09D19460`
found **27 occurrences** — but clustered on only **5 distinct targets**, which already looked wrong for
a per-entry pointer array:

| target | hits |
|---|---|
| `0x09D18C69` | **20** |
| `0x09D172E0` | 3 |
| `0x09D19310` | 2 |
| `0x09D172F0` | 1 |
| `0x09D19320` | 1 |

The decisive property is **alignment**. UTF-16LE text starts on an even address, and
`0x09D18C69` is **odd**:

```
0x09D18C69: even=False -> '━.\x00FReplay repla'
```

Decoding from `0x09D18C69` yields junk and lands on `Replay repl…` at `+1`, which is where the real
string begins. So:

* **0 of 27** values are valid string pointers.
* **Only 7 of 27** are even at all.
* **No pointer arrays** exist — the run detector found **0** arrays with ≥4 consecutive entries.

The 20 "references" to `0x09D18C69` sit in repeated, near-identical 32-byte blocks
(`8B097280 0B86FDCC 09D18C69 …`) at `0x08CC3654`, `0x09407D74`, and elsewhere — duplicated data
structures with byte patterns that look like instruction encodings, not pointer tables. They are
coincidental 4-byte alignments against a value range, not references.

### What this retires

**Menus do not hold pointers to their text.** The contiguous-address observation from section 27 was
correct but the inference drawn from it was wrong: contiguity means the strings are stored as a
**block**, which is consistent with the entries being addressed by **offset/ID arithmetic inside a
loader**, not by stored pointers. This agrees with the earlier independent finding that menus here are
**ID lists** (section 13), and it means the "find the pointer table" approach is now a **dead route**,
not merely an unfound one.

### Corrected framing

The record layout recovered from the static window in this pass is still useful and worth keeping
explicit, since it defines what an ID indexes into:

```
DAT_00397770 + 0x000:  00 00 00 00 00 00 00 00  'one00'  00...
             + 0x02C:  01 00 00 00  'two00'  ...
             + 0x050:  'thr00' ...   +0x04C: 02 00 00 00
             + 0x074:  'for00' ...   +0x090: 03 00 00 00
             + 0x098:  'fiv00' ...   +0x0B8: 04 00 00 00
             + 0x0BC:  'six00' ...   +0x0E0: 05 00 00 00
             + 0x0E4:  'sev00' ...
```

i.e. **36-byte records of `{ int id; char name[32] }`** with an ascending id. So an "index" in this
engine is an **ordinal into a fixed-stride record array**, and the text is resolved through the
loader — which is exactly why no pointer table exists to find.

### Method note

> **When a pointer scan succeeds, check ALIGNMENT before believing it.** A pointer into a UTF-16 table
> must be even; a pointer into an aligned struct should respect the struct's alignment. One parity
> check turned 27 apparent references into **0** references, and would otherwise have sent the search
> down a nonexistent pointer table. Cheap property tests on *candidate* results are as valuable as the
> search itself: a scan's hit count is not evidence until the hits satisfy the type they claim to be.



---

## 29. `DAT_00397770` is the CHAPTER-NAME TABLE, not the manager (two models, one decisive test)

Section 21 reconciled sections 17/20 by concluding that the menu manager is one static struct beginning
with a chapter-name array, so `param_1` and `DAT_00397770` were the same object. Section 27 then
recovered a 36-byte `{ int id; char name[32] }` record layout from it. What was never done is the one
test that separates the two readings: **read the manager's own header fields and see whether they can be
a list.**

### The decompile that made the test possible

`DisDecompile4.java` (which the failing WSL job never ran, but whose report already existed) fixed the
layout of the object the event thunks receive:

* `FUN_0024932c` walks a list from `param_1 + 0x28` (next pointer at `node + 0x24`), and a second list
  from `param_1 + 0x34`.
* `FUN_0024910c` maintains them as a **doubly-linked list** with a free pool at `+0x1490`/`+0x1494` and
  counters at `param_1 + 0x30` and `param_1 + 0x3c`.
* `FUN_0025468c` resolves a node only when `*(ushort *)(node + 0x20) & 1`, via
  `base = *(int *)(table + 0xc) + (short)*(node + 0xc) * 0x1c`, bounds-checked against
  `*(int *)(table + 0x2c)` — **so `node + 0xc` is an INDEX and `table + 0x2c` is a COUNT**.

That gives a sharp prediction: if `DAT_00397770` were the manager, `+0x28` would be a list head and
`+0x2c` a count-or-tail.

### The test — read both models against the same 256 bytes

**Model 1** — 36-byte records `{ int id; char name[32] }`:

```
rec0 @+0x000  name='one00'   OK
rec1 @+0x024  name='two00'   OK
rec2 @+0x048  name='thr00'   OK
rec3 @+0x06C  name='for00'   OK
rec4 @+0x090  name='fiv00'   OK
rec5 @+0x0B4  name='six00'   OK
rec6 @+0x0D8  name='sev00'   OK
   -> 7/7 records parse as {id, printable name}
```

**Model 2** — manager header at the offsets `FUN_0024932c` uses:

```
+0x28 headA   = 0x00000000   ASCII '....'
+0x2C tailA   = 0x306F7774   ASCII 'two0'
+0x30 countA  = 0x00000030   ASCII '0...'
+0x34 headB   = 0x00000000
```

**Model 1 wins 7/7 against 0/1**, and the arithmetic closes it exactly:

> record 1's name field is at `0x24 + 0x08 = 0x2C`, and the bytes at `+0x2C` are literally
> **`b'two00\x00'`** — the name of record 1, `two00`.

So the "list" my first walk of `DAT_00397770` reported was an artefact: it read **string bytes as
pointers**. `count = 48` is the ASCII value of the character `'0'` in `"two00"`, and `tail = 0x306F7774`
is `"two0"`. A list walk over a string table produces exactly this kind of confident nonsense, and the
model test is what exposed it.

### Consequence: the manager is NOT a static struct

This retires the third and final candidate for the manager object. The sequence of eliminations is now:

| candidate | falsified by |
|---|---|
| `DAT_00392cd8` | holds engine-service pointers and a constant 290 that never moved (sections 24, 26) |
| `param_1[8] = -1` | byte `+0x20` = menu-**sound** handle set to unloaded (section 22) |
| `DAT_00397770` | **is a chapter-name table**, 7/7 records (this section) |
| the three registered handlers | draw callbacks over a 4,776-byte renderer (section 27) |

**Every object the constructor touches has now been given a different, verified identity — none of them
is a menu selection container.** So the manager is either reached only through `FUN_00103c60`'s engine
service (`*(param_1 + 0x2000)`, 28 call sites) or it is heap-allocated at menu-open time and has no
static anchor at all. The second is more likely, and it is consistent with the constructor allocating a
`0x40c`-byte object via `FUN_00247b40(0x40c)` and storing it at `param_1[1]`.

### Method note

> **When two structural models fit the same bytes, read the fields each model predicts and see which
> prediction holds.** A list model predicted pointers at `+0x28`; the record model predicted a name at
> `+0x2C` because record 1 starts at `0x24` and names sit at `+0x08`. Seven records parsed and the
> bytes at `+0x2C` were literally `two00` — the models were not equally good, and the test said so in
> one read. **Do not walk a structure as a list until its fields have been shown to be pointers** —
> string bytes reinterpreted as addresses fabricate plausible-looking lists.



---

## 30. The decisive test: compare FILE bytes against RAM bytes

Sections 21-29 argued about which statics hold menu state by reasoning about decompiled fields. That
reasoning kept producing objects that turned out to be something else. There is a direct test that
settles "does this address hold state?" in one read, and it also explains the recurring confusion.

### The ELF's real layout

```
entry = 0x002DC6C8     2 program headers

PT_LOAD  vaddr 0x00000000-0x003A6860   filesz 0x3A6860   RX   (code + rodata)
PT_LOAD  vaddr 0x003A6860-0x01727F8C   filesz 0x1AA0     RW   (initialised data; the rest is BSS)
```

So the **entire 3.7 MB image is one RX segment** and the writable segment is only **0x1AA0 bytes** at
`0x003A6860`, with the remaining `0x01727F8C - 0x003A6860` bytes being **BSS (zero in file, allocated at
run time)**.

This matters because **`DAT_00397770`, `DAT_00392cd8` and `DAT_00392d10` all fall inside the RX
segment** -- they are not writable data in the image. Yet they demonstrably change at run time (below).
The resolution is that the game uses **BSS beyond the RW segment** as scratch, and Ghidra's "static"
labels land in the RX address range while the *live* values come from the run-time allocation. That is
exactly why three separate field-level analyses produced three different wrong identities for objects
near those addresses.

### The test, and its results

Compare the bytes **in the file** against the bytes **in RAM** at the same virtual address. If they are
equal, the location is a read-only constant and **cannot** hold state. If they differ, something wrote
there at run time.

| address | file | RAM | verdict |
|---|---|---|---|
| `0x00008608` | `B8 00 A5 24 00 00 A6 84 …` | identical | **code** — read-only, no state |
| `0x0000860C` | `00 00 A6 84 04 00 A5 8C …` | identical | **code** — read-only, no state |
| `0x00397770` | `00 00 00 00 00 00 00 00 6F 6E 65 30 30 …` | `B0 8E C0 08 00 00 80 3F 6F 6E 65 30 30 …` | **differs → runtime state** |
| `0x00392cd8` | all zeros (16 B) | `80 DC C5 08 00 00 00 00 E8 DC C5 08 …` | **differs → runtime state** |
| `0x09D16A68` (UI text) | not in file (BSS) | `2E 00 52 00 65 00 74 00 …` = `".Ret"` | **runtime state** |

### The correction this forces

**`FUN_00246d64` is not a lazy singleton.** Its body reads `if (iRam00008608 == 0) { iRam00008608 = 1;
FUN_0024699c(0x860c); FUN_0033c4d0(&DAT_003976c0); } return 0x860c;` -- but `0x00008608` and `0x0000860C`
are **identical in file and RAM**, i.e. they are **instructions**, and the walk of `0x0000860C` returned
`+0x3C = 0x03E00008`, which is the MIPS encoding of **`jr $ra`**. Reading an object there produced
instruction words reinterpreted as head pointers (`headA=0xC4CC44F4`, `headB=0x00872021`),
the same failure mode as reading string bytes as pointers in section 29.

The general lesson is now unambiguous: **a "static" address in a disassembly listing is not necessarily
data.** On an RX segment, a plausible-looking field can be executable code, and the file-vs-RAM
comparison is the cheap way to find out before building any model on top of it.

### What is also revealed about `DAT_00397770`

Its leading 8 bytes are **written at run time**:

```
file: 00 00 00 00  00 00 00 00  6F 6E 65 30 30   ("one00")
RAM : B0 8E C0 08  00 00 80 3F  6F 6E 65 30 30
      ^ pointer    ^ float 1.0
```

So the 36-byte record's first two words are **runtime fields** (a pointer and, in record 0, the float
`1.0` = `0x3F800000`) followed by the static name. The *name* part is genuinely read-only chapter data,
which is why section 29's 7/7 record parse was correct; the *head* of each record is per-run state and
was not part of that model. Both findings stand, and this explains why: the model was tested on the
name field, which is static, while the fields around it are not.

### Method note

> **To ask "does this address hold game state?", compare the file bytes with the RAM bytes at the same
> virtual address.** Equal ⇒ read-only constant, and no amount of field-level reasoning will find state
> there. Different ⇒ something writes it. Run this test *before* modelling a location, because on this
> binary the RX segment contains labelled "statics" that are really code and really BSS scratch, and
> three prior identity conclusions were wrong for exactly that reason.



---

## 31. A controlled press-diff: press-responsive fields found, but NO selection index

Sections 24-30 tried to find the highlight by reasoning about decompiled objects. This section uses a
different method entirely -- **difference a bounded writable region across one press, then control for
churn** -- and it produces a clean, well-controlled negative.

### Why the method is different from the failed early scans

Earlier full-RAM differencing produced 174,900 and 75,223 "fixed triples" dominated by static code,
because the whole readable span was filtered for float shapes. Section 30 explained why that could
never work: the EBOOT is **one RX segment (0x00000000-0x003A6860) plus a small RW segment**, so most of
the span is immutable. This method diffs only a bounded region that can plausibly hold state and uses
**one press**, not thousands of frames.

### Step 1 -- region diff across a single `down`

`psp-diff-press.py --press down --lo 0x08BA6860 --hi 0x08D20000` (3.5 MB), with the screen capture
verified before and after so the press could not silently fail:

```
screen CHANGED (press landed)
4-byte words that changed: 4109
clusters (gaps < 0x40):
   0x08BB3100..0x08BB70FC  4096 word(s)     <-- one 16 KB scratch buffer
   0x08BFF928..0x08BFF980    13 word(s)
   0x08C01A9C..0x08C01B14    17 word(s)
   ... plus ~19 SMALL clusters of 1-3 words
```

The 4096-word cluster is a single contiguous 16 KB buffer (frame/audio scratch) -- unrelated churn. The
**1-3 word clusters are the size a selection index would be**, so those became the candidates.

### Step 2 -- watch the candidates across 5 presses

`psp-watch-words.py --press down --steps 5` over 17 candidate words, classifying each series:

```
0x08BFC194  146798928 146798984 146799040 146799096 146799152 146799208  step 56
0x08BFEA28  146798928 146798984 146799040 146799096 146799152 146799208  step 56
0x08C00A28  146786392 146786400 146786408 146786416 146786424 146786432  step 8
0x08C01990  146807512 146807576 146807640 146807704 146807768 146807832  step 64
0x08C043D8  146803584 146803592 146803600 146803608 146803616 146803624  step 8
0x08BAC75C  0 1 0 0 1 1        <- 0/1
0x08BF9700  0 1 0 0 1 0        <- 0/1
```

### Step 3 -- the control that makes it meaningful

The same six samples taken **with no press at all**, at the same 1.1 s interval:

```
0x08BFC194  [146799208 x6]  steps=[0]
0x08BFEA28  [146799208 x6]  steps=[0]
0x08C00A28  [146786432 x6]  steps=[0]
0x08C01990  [146807832 x6]  steps=[0]
0x08C043D8  [146803624 x6]  steps=[0]
0x08BF8D68  steps=[1134466, 1134467]   <- free-running (not press-related)
```

**So the words are genuinely press-responsive: constant without presses, advancing by a fixed step with
them.** They are not timing counters, and the press is definitely reaching the game's data.

### The negative, and why it is informative

**All five press-responsive words are pointer-scale, not ordinals:**

```
146798928 = 0x08BF...
146786392 = 0x08BF...
146807512 = 0x08C0...
```

A selection index is a *small* integer (0..20). These increments of 8 / 56 / 64 are consistent with a
**bump allocator or record cursor advancing one entry per press**, i.e. press-driven allocation inside
arrays whose record strides are 8, 56 and 64 bytes. Notably **`0x08BFC194` and `0x08BFEA28` hold the
*same* value and step together**, which is what two aliases into one structure look like.

So: **the press registers, the game does per-press work, and no small-ordinal field moved anywhere in
the 3.5 MB writable region.** For the PAUSED menu that is consistent with the screen evidence -- `down`
changed the display only 1 time in 3 earlier rounds, so this menu does not scroll with `down`. The
correct conclusion is that the **pause menu is a poor target**, not that the cursor is absent from the
game.

### What the method is now good for

The pipeline is proven and cheap, and it needs only a screen that actually scrolls:

1. reach a **static UI screen** (`psp-reach-menu.py` finds it and names the button);
2. diff a bounded region across **one press of the control that screen uses** (`psp-diff-press.py`);
3. keep only the **small clusters** (discard any multi-hundred-word contiguous buffer);
4. watch them across repeated presses **and run the no-press control** (`psp-watch-words.py`);
5. accept only a **small-ordinal** series -- pointer-scale values are allocators.

Step 4 is the one that cannot be skipped: without it, a free-running counter advancing once per press
interval looks exactly like a cursor following the highlight.

### Method note

> **A field that moves when you press is not a cursor until you show it does NOT move when you don't.**
> The no-press control promoted five words from "suspicious" to "definitely press-driven", and their
> *magnitude* then demoted them from cursor candidates to allocator cursors. Both checks are cheap and
> either one alone would have produced a wrong answer.



---

## 32. Screen identification by "which strings are resident" does NOT discriminate — superseded

Section 31 left navigation dependent on OCR, which cannot read Dissidia's stylized title/mode font. The
natural fix is to identify the current screen by which UI strings are resident in RAM instead. **That
was tried and it is invalid.** Recorded because the failure is instructive and the tool is kept as a
marked trap.

### The test, and its result

`psp-screen-id.py` scanned the readable span for distinctive markers per screen and reported:

```
title / mode menu        dynamic=5  MATCH   Museum, Shop, Options, Story, Battle, Arcade Mode, Lobby
story / region map       dynamic=3  MATCH   Level Progression, Story Mode, storypoint, Quit Level Progression
pause menu               dynamic=6  MATCH   Return to Game, Retry, Quicksave, Help Manual, Skip Cutscene
battle (tutorial/help)   dynamic=3  MATCH   HP attacks, EX Mode, EX Burst
save / load dialog       dynamic=4  MATCH   GAME DATA, Memory Stick, autosave, Saved data is corrupt
result screen            dynamic=2  MATCH   Continue to Next Battle, DP
data setup / first run   dynamic=0  -
```

**Every screen matched.** The tool cannot distinguish the pause menu from the title menu, a battle, a
save dialog or a result screen.

### Why — and it is the fourth instance of one error

The markers are real and were found at real addresses:

```
Return to Game        0x09D16AFC   (pause menu marker)
Help Manual           0x09D16D70   (pause menu marker)
Continue to Next Battle 0x09D17046 (result-screen marker)
Museum                0x09D16F36   (title-menu marker)
```

They all resolve into the **one contiguous UI text pool at `0x09D16A68`+**, which holds the whole menu
string set at once, with further pools at `0x09A3Fxxx`, `0x09A40xxx` and `0x09E58xxx`. So a string's
presence carries **no information about the current screen**.

This is the same mistake as section 25 ("the region assets are resident, so story mode is loaded"), and
it is now the **fourth** time a resident string has been treated as evidence of state (see also sections
13, 27). It also refines the earlier rule in an important way:

> **"Absent from the EBOOT" is necessary but NOT sufficient for "indicates current state."**
> These strings are *not* in `EBOOT.BIN.dec` — they are loaded into RAM by the resource loader — and they
> are *still* not per-screen. The earlier formulation (section 25) was too weak in one direction and too
> strong in the other: the ELF test rules out static constants, but surviving it does not prove that a
> string tracks anything.

### What actually does discriminate

| approach | verdict |
|---|---|
| resident UI strings | **no** — all screens resident at once (this section) |
| EBOOT-resident test alone | **necessary, not sufficient** |
| **OCR of on-screen banners** | **yes** — OCR reliably read `PAUSED` on the pause menu; that is a positive identification |
| **frame structure** | **yes** — two-frame diff + saturated-colour % separates "menu" from "scene" (already correct in `psp-reach-menu.py`) |
| a screen INDEX field | would be ideal; not located, and sections 27-31 show the manager has no static anchor |

So the practical recipe is: use **frame structure** to tell menu from scene, **OCR of the banner/plain-font
text** to name the screen, and **RAM text** only to read the *contents* a screen is showing — never to
infer *which* screen is showing.

### Disposition

`psp-screen-id.py` is replaced with a stub that documents the failure and `exit(1)`s. Keeping the
non-discriminating implementation as a working script would make it a trap for a future session, so the
marking is the point.

### Method note

> **A probe must be validated against a case where the answer is known to be NO.** This tool reported
> six simultaneous positives, and that alone proved it was broken — no screen is six screens at once.
> When a detector returns everything, the detector has no discriminating power; the fix is to find a
> signal that is *absent* on the other screens, not to add more markers.



---

## 33. CORRECTION: the pause menu DOES scroll — the earlier "does not respond to down" was an artefact

Section 31 concluded that "the pause menu is a poor target" because `down` changed the display only 1
time in 3 rounds, and section 32 built on that. **That conclusion was wrong**, and it was wrong because
of the measuring instrument rather than the game. This section documents the correction because it
reopens a line the previous two sections had closed.

### Why the earlier measurement failed

The "does not respond to `down`" finding came from a **coarse 8x8 luminance grid hash** (64 cells). A
menu highlight is a **small, localized** change -- a few hundred moving pixels -- and averaging each cell
over a 213x133 region can erase it entirely. The instrument could not see the thing it was measuring.

### The full-resolution measurement

`psp-scroll-test.py` counts exact changed pixels between consecutive captures, with a **no-press control**:

```
CONTROL (no press)   changed pixels: 0        bbox: None
WITH PRESS (down)    round 1: 247189 px   bbox (478, 472, 1705, 720)
                     round 2: 247189 px   (same bbox)
                     round 3: 247189 px   (same bbox)
                     round 4: 247189 px   (same bbox)
```

The control is **exactly 0**, so the screen is genuinely static and the press is genuinely doing
something. The changed region is a **~1227x248 panel** (x 478-1705, y 472-720) -- text-sized, at the
bottom of the screen. Capturing at increasing delays after a single press showed the change **persists**
(t1..t4 all differ from t0 by the same ~247k px), so it is a real state change, not a frame effect.

### What the panel says -- and the answer

Cropping that exact region and OCRing the two states:

```
t0 (before press):    "4 Return to Game"
t1 (after press):     "Return to Game"  /  "4 Retry"
```

**The highlight moved from "Return to Game" to "Retry".** The `4` is the cursor marker rendered beside
the selected line, and it moved down one row. So:

> **The pause menu scrolls with `down`, and the selection is a row index that advances by exactly one
> per press.** The changed-pixel count is identical every round because the panel alternates between
> two visually similar states (highlight on row N vs row N+1) as the press-diff is taken between
> adjacent states.

### Consequence for the search

This **reopens** the press-diff route that section 31 closed. Its premises were:

* "`down` does not change the display" -- **false** (this section);
* "no small-ordinal field moved anywhere in the 3.5 MB region" -- measured *against that false premise*,
  with the diff taken between captures that may not have bracketed the state change.

So the section 31 negative is **not trustworthy** and must be re-run, this time bracketing the state
change properly: capture, press, capture (allowing for the state to settle), and require the screen to
have actually changed. Concretely, the re-run should:

1. confirm the highlight moved via the panel crop (as above), not a grid hash;
2. diff only around the press, after confirming two *distinct* highlight positions are being compared;
3. look specifically for a **small ordinal (0..5)** -- the pause menu has ~6 rows -- rather than any
   press-responsive value.

### Method note

> **A coarse summary statistic can silently erase the signal.** An 8x8 grid hash was adequate for
> "menu vs scene" and completely inadequate for "did a highlight move one row". The same probe design
> must be re-checked for sensitivity whenever it is reused for a finer question -- and this is the second
> time in this investigation that a *negative* result was an instrument artefact rather than a fact
> about the game (the first being the "no change" sweep whose screenshot capture had crashed).



---

## 34. An unreproduced transient, and the over-reading it caused

Section 33 reopened the press-diff route and section 34's run produced what looked like the answer:
small-ordinal fields moving through a strided array. **It did not survive reproduction**, and the
reason is a sampling error worth recording.

### What the corrected run reported

`psp-diff-press2.py --press down --lo 0x08BA0000 --hi 0x08D20000 --rounds 3` (4 samples: baseline plus
three presses), comparing only words that moved and stayed in a small range:

```
SMALL-ORDINAL 0x08C01CFC  [0, 1, 0, 0]
SMALL-ORDINAL 0x08C01D3C  [0, 1, 0, 0]
SMALL-ORDINAL 0x08C01D7C  [0, 1, 0, 0]
SMALL-ORDINAL 0x08C01DBC  [0, 1, 0, 0]
SMALL-ORDINAL 0x08C01DFC  [0, 0, 1, 0]
SMALL-ORDINAL 0x08C01E3C  [0, 0, 0, 1]
SMALL-ORDINAL 0x08C01E40  [0, 0, 0, 1]
   -> 8 small-ordinal field(s)
```

Reading it as a series, the values looked like a **one-hot marker walking one record per press**, at a
record stride of `0x40` -- i.e. exactly a selection cursor advancing through menu rows.

### Why that reading was wrong

Two checks falsified it.

**1. The 10-press follow-up reproduced nothing.** Watching the word at `record + 0x1C` for ten records
across ten presses:

```
press#   r0  r1  r2  r3  r4  r5  r6  r7   r8        r9
  0      0   0   0   0   0   0   0   0   164748704   0
  1      0   0   0   0   0   0   0   0   0         164748704
  2..10  0   0   0   0   0   0   0   0   0           0
```

No one-hot pattern at all.

**2. Re-reading the window shows a fixed structure, not a moving marker.** `0x08C01C00`/`0x08C01D80` are
identical `0x40`-strided record arrays with constant contents:

```
record +0x00 = -1        +0x0C = 2097344      +0x20 = 164748704
record +0x24 = 11010065 or 11272210           +0x28 = 16908466 or 16908470
record +0x2C = 145008992
```

and the specific addresses flagged (`0x08C01DBC`, `0x08C01DFC`, `0x08C01E3C`) read **0** now.

### The sampling error

> **With only four samples, "one-hot" and "four independent transients" are indistinguishable.**

The tool compared 4 samples. A transient field that is `1` in exactly one sample and `0` in the other
three produces the pattern `[0,1,0,0]` -- and three *different* such fields produce
`[0,1,0,0] / [0,0,1,0] / [0,0,0,1]`. That is exactly what was observed. The `0x40` spacing of the
addresses then has a second, simpler explanation: the region contains **fixed `0x40`-strided record
arrays**, so any two transients sampled from that region are `0x40` apart by construction. Both halves
of the "walking marker" interpretation have a boring explanation that fits the same data.

A one-hot series requires **more samples than there are positions**. Four samples cannot show a marker
visiting six menu rows, and cannot distinguish a walk from noise.

### The requirement this imposes

The press-diff route stays live (section 33's correction stands -- the menu does scroll), but any future
run must:

* take **many more samples** than the expected number of positions (the pause menu has ~6 rows, so
  **≥12 presses**), and require the flagged field to be `1` in a *sequence*, not merely non-constant;
* **reproduce the pattern** in a second independent run before treating it as the cursor;
* prefer **one field followed through the whole sequence** over cross-sectional comparisons of many
  fields, since the latter cannot tell a walk from a set of coincidences.

### Status

**No confirmed cursor.** What section 34 adds is negative but load-bearing: it removes a false positive
that a 4-sample diff had produced, and it fixes the sampling requirement that let it through. Combined
with section 33, the pause menu scroll is confirmed and the search method is sound -- what was wrong was
the number of samples, not the target.

### Method note

> **A pattern needs more samples than the pattern has states.** "One-hot walking" was inferred from four
> observations of a structure with many positions. Any inference about a *sequence* from samples fewer
> than its period is unsupported -- and the fix is to count the positions first, then sample past that
> count, then reproduce.



---

## 36. ROOT CAUSE: hammering region reads on a PPSSPP debugger connection suppresses button presses

Sections 34 and 35 produced contradictory results -- manual presses moved the highlight, but the tool's
presses appeared to do nothing -- and section 35's refusal guard caught the contradiction. The cause is
now identified and reproduced, and it invalidates several earlier runs.

### The reproduction

Three cases, all verified by comparing shell-captured screens:

| case | press mechanism | result |
|---|---|---|
| manual (shell) | separate `psp-press-sweep.py` process | **247,189** px changed |
| tool-like | press on the **same socket** after 20 region reads | **8,024** px changed |
| fixed | press on a **separate debugger connection** after 20 reads | **247,189** px changed |

So a button press sent on the **same connection that is being hammered with `memory.read`** is delivered
at roughly **1/30th** of its effect -- the highlight does not move. Using a **second connection** for
input restores full delivery.

### Why this matters more than a nuisance bug

Every press-diff run in sections 31, 34 and 35 sent presses on the connection it was also reading
through. Concretely:

* **Section 34's re-run** saw `0 changed pixels` across all 12 presses -- the presses were effectively
  swallowed, and the script analysed anyway, producing the `[0,1,0,0]/[0,0,1,0]/[0,0,0,1]` pattern that
  had to be retracted.
* **Section 31** did observe screen changes and reported small-ordinal movers, but a suppressed press
  produces a *partial* change (~8k px rather than ~247k px). Those "press-responsive" small ordinals are
  therefore **not safely attributable to a moving highlight** -- they may be the game's response to a
  partially delivered input.
* The **no-press control** in section 31 remains valid (it showed 0), so the fields were genuinely
  press-related; what is now doubtful is *what* they were tracking.

So this is the fourth instrument defect in this investigation, and like the others it produced a
**confident wrong answer** rather than an obvious failure.

### The fix

`psp-diff-press2.py` now refuses to analyse when no press moved the display (implemented in section 35,
and what caught this case), and the corrected procedure is:

> **Use a dedicated debugger connection for input, separate from the connection used for memory reads.**

Verified working: reader connection hammered with reads + a second connection for the press yields the
full 247,189-pixel change, i.e. the highlight really moves.

### Consequence

The press-diff route must be re-run with the two-connection split before any of its results are believed.
The corrected sequence is:

1. reach a **scrolling** menu and confirm the highlight moves (`psp-scroll-test.py`, or OCR the panel);
2. hold a **read connection** for RAM and a **separate input connection** for presses;
3. take **at least 12 presses** (more samples than menu rows -- section 34's rule);
4. require the screen to change on each press (section 35's guard);
5. follow **one** field through the full sequence, then reproduce it.

### Method note

> **A tool's I/O can interfere with the very thing it measures.** Here the measurement channel (bulk
> `memory.read`) degraded the control channel (button input) on the same connection, at a 30x effect
> ratio -- enough to make a working menu look inert, and small enough to survive as a plausible partial
> response. When a probe both drives and observes, **separate the two paths and verify the drive
> independently**; and keep the guard that refuses to analyse when the expected effect did not appear,
> because that guard is what surfaced this.



---

## 37. The corrected press-diff found a real press-driven walk — and the wrap test rejected it

Sections 31-36 progressively fixed the instrument. With all three faults repaired the press-diff finally
produced a controlled result, and the **wrap test** then ruled the candidate out. Both halves matter.

### The corrected run, with every fault fixed

`psp-diff-press3.py`: **two debugger connections** (reader + presser, section 36), **1.5 s settle** after
each bulk read before capture (section 36), **12 samples** for a ~6-row menu (section 34), and a **refusal
guard** if no press moved the display (section 35).

**Every press was verified delivered:**

```
press 1 :   247209 px        press 7 :   247189 px
press 2 :   247209 px        press 8 :   247189 px
press 3 :   247189 px        press 9 :   247189 px
press 4 :   247313 px        press 10:   247189 px
press 5 :   247313 px        press 11:   247189 px
press 6 :        0 px <-- highlight already at a list boundary
press 12:   247209 px
```

Only one press in twelve moved nothing (the boundary), and the log makes that visible instead of silent.

### The candidate: a one-hit-per-press walk

```
0x08C023BC  [1,0,0,0,0,0,0,0,0,0,0,0,0]   press 1
0x08C023FC  [0,1,0,0,0,0,0,0,0,0,0,0,0]   press 2
0x08C0243C  [0,0,1,0,0,0,0,0,0,0,0,0,0]   press 3
0x08C0247C  [0,0,0,1,0,0,0,0,0,0,0,0,0]   press 4
0x08C024BC  [0,0,0,0,1,0,0,0,0,0,0,0,0]   press 5
0x08C024FC  [0,0,0,0,0,1,1,0,0,0,0,0,0]   press 6   (boundary: two set)
0x08C0253C  [0,0,0,0,0,0,0,1,0,0,0,0,0]   press 7
```

A second block filled one entry per press as well, by an alternating stride:

```
0x08C00134  [0,18,18,18,...]     0x08C001A4  [0,0,0,18,18,...]
0x08C0016C  [0,0,17,17,...]      0x08C001DC  [0,0,0,0,17,...]
```

### The no-press control: clean

The same fields sampled eight times with **no press at all**:

```
0x08C023BC..0x08C025FC   all 0
0x08C00134   18 18 18 18 18 18 18 18
0x08C0016C   17 17 17 17 17 17 17 17
0x08C001A4   18 18 18 18 18 18 18 18
```

So the walk is genuinely press-driven, and the 17/18 block holds steady values when nothing is pressed.
This is a much stronger result than any earlier attempt: real effect, clean control, verified delivery.

### The wrap test rejects it

The candidate index advances without bound:

| presses | slot holding 1 |
|---|---|
| 12 | 11 |
| 22 | **20** |

**Maximum index 20 across 22 presses**, monotonic throughout. But the **visible menu wraps at about two
items** -- OCR of the panel showed `Retry` -> `Return to Game`. A selection cursor must wrap at the list
length; this one increments indefinitely.

**Conclusion: it is a press counter / event index / growing list index, NOT the highlight.**

The distinction is exactly the one section 33 established in reverse: measure the *game's* wrapping
behaviour from the screen, then require the memory candidate to match it. Here the screen said "wraps at
2" and the candidate said "wraps at never", and that single comparison settled it.

### Where this leaves the cursor

* The **measurement pipeline is now sound** and this is the first run where that is true: verified
  delivery, clean control, adequate sampling, refusal guard.
* The **cursor remains unfound**, and what the pipeline returns from `0x08BE0000-0x08C20000` is
  allocation/list bookkeeping rather than navigation state.
* The wrap test gives a decisive, cheap filter for the next candidate: **press more times than the menu
  has rows and require the index to return to its start.**

### Method note

> **Derive the expected behaviour from the SCREEN, then require it of the candidate.** The screen showed a
> 2-item wrap; the memory candidate never wrapped; so it is not the same quantity. Comparing a candidate
> against the game's own observable behaviour is stronger than any amount of internal plausibility --
> and it is cheap, because the screen was already being OCR'd.



---

## 38. The region is cleared: NO wrapping field, so the cursor is not in `0x08BE0000-0x08C20000`

Section 37 ended with a decisive filter: a selection cursor must **return to a value it already held**
once presses exceed the menu's rows. This section applies it as a positive test over the whole region.

### The test and its result

`psp-find-wrapping.py --press down --rounds 20 --maxperiod 12`: 20 presses (well past the ~6 visible
rows), three instrument fixes in place (two connections, settle before capture, movement guard), and a
report of **only** words whose series repeats within 12 samples.

```
presses that moved the display: 20/20

=== words whose series REPEATS (a wrapping index) ===
   NONE: no word in this region repeats within period 12
```

**Zero wrapping fields in the entire 0.25 MB region.** Delivery is now proven solid (20 of 20 presses
moved the display -- the best rate of any run in this investigation), so this is a fact about the
region, not another instrument fault.

### What the region actually holds

Combining this with sections 36-37, every press-responsive field in `0x08BE0000-0x08C20000` is
accounted for, and none is navigation state:

| field(s) | behaviour | identity |
|---|---|---|
| `0x08C023BC`..`0x08C026C0` | one-hot marker advancing one slot per press, **index 20 across 22 presses, never wraps** | press counter / growing list index |
| `0x08C00134`..`0x08C00364` | one entry fills per press, then holds 17/18 forever | event log / allocation record |
| `0x08BF9700` | alternates and reverts | toggle flag |
| 3815 pointer-scale movers | advance by 8/56/64 | bump allocator / pool churn |

All monotonic, all explained, none wrapping.

### Conclusion, stated precisely

**The selection index is not in this region.** Two possibilities remain, and they are now distinguishable:

1. the menu's row count exceeds 12, so a wrapping cursor would need a longer sample; or
2. the cursor lives **outside** this address range -- most likely in the **heap** where the manager was
   allocated, which sections 27-29 concluded has **no static anchor**.

Since section 37 showed the visible menu wraps at roughly **2-6 rows**, possibility 1 is unlikely: a
period of 2-6 would have been detected by `maxperiod=12`. So **possibility 2 is the live hypothesis**, and
it is consistent with the earlier structural conclusion that the menu manager is allocated at menu-open
time rather than living in a static.

### The next step this implies

Find the **heap allocation** the constructor made. `FUN_002489d0` allocated a `0x40c`-byte object via
`FUN_00247b40(0x40c)` and stored it at `param_1[1]`. On the **live pause menu**, resolve that pointer and
read it -- that is where a heap-resident menu object's selection field would be. This is a targeted read
of a known pointer, not another blind region diff.

### Method note

> **A positive test beats a growing exclusion list.** Rather than continuing to catalogue what each
> press-responsive field *is* (sections 36-38 did that for four classes), state the one property the
> target must have -- here, periodicity -- and test the whole region for it in a single run. "No word
> repeats within period 12" clears 0.25 MB at once and, combined with the screen's own wrap behaviour,
> tells you the target lies outside the range. Clearing a region is progress: it converts "not found
> here" into "not here."



---

## 39. The manager object is located and its list walked — the decompiled layout is CONFIRMED, and the list is static

Section 38 ended by proposing a targeted read: follow the heap pointer the constructor stored. That was
done, and it produced the strongest structural confirmation so far -- plus another cleared structure.

### Finding the manager object

`DAT_00397770 + 0` holds a heap pointer the ELF has as zero (`0x08C08EB0`). Following it:

```
DAT_00397770 +0x00 = 0x08C08EB0   (heap pointer, written at run time)
             +0x04 = 0x3F800000   (float 1.0)
             +0x08 = 'one0'        (chapter-name data begins -- section 29)
```

and the object at `0x08C08EB0`:

```
+0x20 = 0xFFFFFFFF   <- -1   sentinel
+0x24 = 0x00000001   <- 1    count
+0x28 = 0x08C093F4   <- head
+0x2C = 0x08C093C8   <- tail
+0x30 = 0x00000023   <- 35   count / stride
+0x3C = 0x00000014   <- 20
```

**These are exactly the manager-header offsets used by the decompiled code.** `FUN_0024932c` walks a
list from `param_1 + 0x28`; `FUN_0024910c` maintains head/tail and decrements counters at
`param_1 + 0x30` and `param_1 + 0x3c`; the constructor sets `param_1[8] = -1`. So `0x08C08EB0` **is** the
manager-class object, and section 38's hypothesis (a heap-allocated manager with no static anchor) is
confirmed: the static holds only a **pointer** to it, written at run time.

### The list walks correctly, and confirms the decompile

`psp-walk-nodes.py --mgr 0x08C08EB0`:

```
head=0x08C093F4 tail=0x08C093C8 count=35 nodes=35

0x08C093F4  def=146886532 flags14=0x03 f17=0 w20=0x0000 next=0x08C09FFC child=0x00000000
0x08C09FFC  def=146883864 flags14=0x03 f17=0 w20=0x0000 next=0x08C09FD0 child=0x00000000
0x08C09FD0  def=146883748 flags14=0x03 f17=0 w20=0x0000 next=0x08C09FA4 child=0x08C165E8
... 35 nodes ...
```

The head/tail values match the header fields, the count matches, and the nodes link through `+0x24`
exactly as `FUN_0024910c` unlinks them. **So the decompiled list layout is now validated against live
data** -- head `+0x28`, tail `+0x2C`, count `+0x30`, next `+0x24`, flags `+0x14`, definition pointer
`+0x0C` (recall `FUN_0025468c` reads `iVar3 = param_1[3]` = `+0x0C`), child list `+0x3C`.

This is a real result: four prior sections reasoned about this layout from decompile text alone.

### ...and it is completely static

Three verified presses (247,209 px each, highlight moving `Retry` <-> `Return to Game`) changed
**nothing**: the header fields and every node field were identical, and the node count stayed 35.

```
=== node fields that changed across presses ===
   (no node field changed)
```

Also note **35 items is not the pause menu** (about 6 rows). So this list is the menu **definition
table** -- all items the manager knows about -- not the visible list. That explains both its size and its
static nature.

### Where this leaves the cursor

Every structure reachable from the manager is now either explained or static:

| structure | status |
|---|---|
| `DAT_00392cd8` | engine-service pointers + constant 290 (s24, s26) |
| `DAT_00397770` head | chapter-name table, 7/7 records (s29) |
| `DAT_00397770 +0` | pointer to the manager (this section) |
| manager header `+0x20/0x24/0x30/0x3C` | **static** across 14 presses (this section) |
| manager list, 35 nodes | **static**, every field (this section) |
| `0x08BE0000-0x08C20000` | no wrapping field (s38) |
| three registered handlers | draw callbacks (s27) |

**The highlight is stored somewhere none of these reach.** The remaining candidate class is the
**per-screen / per-frame working set** the draw callback builds -- which is consistent with section 27's
finding that the handlers fire a fixed render sequence rather than holding state.

### Method note

> **Walking a structure validates the decompile as a side effect.** Reading the manager through
> `+0x28`/`+0x2C` returned head/tail/count that agreed with the header fields and linked 35 nodes
> through `+0x24` -- which confirms four sections of decompile-derived reasoning in a single read. A
> structure that walks cleanly is strong evidence the layout is right, **even when the field you want
> turns out not to be in it.** Say so explicitly rather than reporting only the negative.



---

## 40. The cursor is TRACKED: a pointer array whose entries alternate with the highlight

After 39 sections of clearing data structures from the decompile side, a full-RAM scan at **byte
granularity with a no-press control** found the first data in this investigation that actually follows
the cursor.

### How it was found

The earlier word-level wrap scan (section 38) covered only 0.25 MB. Measured throughput is **~3 MB/s**,
so a 24 MB sample costs only ~8 s -- which made a whole-span scan practical. Two scans followed:

* **word-level, all readable RAM** (6,291,456 words x 21 samples, 17/20 presses moving the display):
  **no wrapping word anywhere.**
* **byte-level, all readable RAM** (25,165,824 bytes x 21 samples), with every sample **saved to disk**
  for offline re-analysis, and a **no-press control** run (6 samples) to reject churn.

Gap this closed: a selection index is naturally a **byte**, and if the neighbouring bytes of its 32-bit
word churn every frame then the *word* never repeats even though the *byte* does -- so a word-level
period test cannot see a byte-sized cursor.

### The filter that isolated it

1. byte-level period-2 series with small values -> 73 candidates, **all** absent from the no-press
   control (so genuinely press-related);
2. widen to **word** level, any magnitude -> 503 press-only period-2 words;
3. classify by value kind -> **3 words whose two values are both RAM pointers**;
4. verify with a 4-press watch.

### The candidates, verified

```
0x09DEE4B0   0x09E36568 <-> 0x09E36558
0x09DEE4B4   0x09E3739C <-> 0x09E3735C
0x09C5825C   0x08C50019 <-> 0x08C50018
```

watching them across four `down` presses:

```
A  A  B  A  B        <- exactly the 2-item cycle observed on screen
```

This is the first memory data that **tracks the visible highlight**.

### What the structure is

`0x09DEE480` is a clean array of **16-float transform blocks** with a three-pointer tail:

```
0x09DEE4B0   09E36558   09E3735C   08C11F3C     <- cursor candidate block
... next block at +0x50 ...
0x09DEE500   09E00CC4   09E01400   08C10028
0x09DEE550   09E00CDC   09E01460   08C105BC
```

Each block is 16 floats (four `1.0` / `-1.0` / `0.0` groups -- a matrix or orientation set) followed by
three pointers. In the block that tracks the highlight, the **first two pointers alternate** and the
**third is stable**.

### Honest status: TRACKED, not yet decoded

Stated precisely, because the distinction matters:

* **Established:** these words carry information about the current highlight and flip with it. They are
  press-driven, absent from the no-press control, and they cycle in step with the on-screen selection.
* **Not established:** that they are the canonical selection index. They may be *derived* render state
  (a transform for the highlighted row, rebuilt when the selection changes) rather than the navigation
  state itself. With a **2-item** menu, a derived value and an index are indistinguishable -- period 2
  is the only signal a 2-item list can give.
* **Next step, and it needs a bigger menu:** on a menu with **more than 2 items**, a true index would
  show period `N` and step 1, while derived transform state would show something else. This menu cannot
  answer the question, so the target must change to the **title menu** (Story / Battle / Customize /
  Museum / Shop / Options / Data).

### Method note

> **Save every sample.** The byte-level scan wrote all 21 samples to disk, which is what made the
> subsequent word-level and kind-classification passes possible **without touching the emulator again**.
> The candidate was found on the *third* analysis of the same data. Re-reading would have cost minutes
> per hypothesis; re-analysing a saved array costs milliseconds.

> **A control at the right granularity.** The no-press run rejected 0 of 73 candidates because they were
> all genuinely press-related -- which is itself the useful result: it means the filter was selecting
> real signal, not churn, and it promoted the 3 pointer-valued words from "suspicious" to "the cursor's
> neighbourhood".



---

## 41. RESOLVED: the block array is RENDER state, not the navigation index

Section 40 flagged the honest ambiguity: the three alternating pointers might be the selection index or
**derived render state**. Two targeted reads resolve it in favour of render state.

### Localisation: only two bytes change per press

Within `0x09DEE480-0x09DEE880` (1024 bytes), a byte-level diff across presses found **exactly 2 bytes
changing**:

```
press 1:  +0x030  0x58 -> 0x68     +0x034  0x5C -> 0x9C
press 2:  0 bytes differ
press 3:  +0x030  0x68 -> 0x58     +0x034  0x9C -> 0x5C
```

So only the **low bytes of the two pointers at block offset `+0x30` and `+0x34`** move. The array
itself is clean: **stride `0x50`**, 13 blocks, each *16 floats (a transform/orientation set) followed by
three pointers*. Pointer word offsets sit at `+0x30, +0x80, +0xD0, +0x120, ...` -- i.e. every `0x50`
starting at `+0x30`, one pointer triple per block.

### What the pointers point at

Following them:

```
0x09E36558: 00050102 09E37358 BF8001FF 09E37378 |   0.000   0.000  -1.000   0.000
0x09E3735C: 000A0000 03700400 BF800000 00000000 |   0.000   0.000  -1.000   0.000
            3F800000 3F800000 FF808080 00830004 |   1.000   1.000     nan   0.000
```

These are **quad / display-list records**: flag-and-float pairs (`00050102`, `BF8001FF`), consecutive
sentinels (`-1.000`, `1.000`), and a **pointer chain** `09E37358 -> 09E37378 -> 09E37398 -> 09E373D8 ->
09E37418 -> 09E37438` at stride `0x20`. Reading the same address twice shows the **contents are
identical** while the pointer moves -- the data is static geometry, the pointer selects which geometry.

### Conclusion

**The block array at `0x09DEE480` is a render-node array.** Each block is a transform plus per-item
geometry pointers, and the block that tracks the highlight does so because the highlighted row's
geometry is rebuilt when the selection changes. It is **derived render state, not the navigation
index** -- exactly the alternative section 40 could not rule out, now ruled in.

This also explains the period-2 flip cleanly: with two rows there are two geometries, and the pointer
swaps between them. It says nothing about where the *selection* is stored, because a renderer can be
correctly driven from an index that lives anywhere.

### What this closes and what it opens

**Closes:** the last remaining candidate from the RAM side. Every structure is now identified:

| structure | identity |
|---|---|
| `DAT_00392cd8`, `DAT_00397770` | engine handles / chapter table |
| manager header + 35-node list | static definition table |
| `0x08BE0000-0x08C20000` | no wrapping field |
| **`0x09DEE480`** | **render-node array (derived)** |

**Opens:** the search must move to **code**. Something writes the selection that drives this array, and
the input path is now mapped:

* `FUN_000f7994` calls `FUN_0036da44(1)` / `FUN_0036da54(0x411b)` (sceCtrl sampling setup) and registers
  **`FUN_000f790c`** through `FUN_00103db0` -- **the same registration mechanism the menu manager uses**;
* `DAT_003925b0` is the global pad object (`+0x264` = input flags); `FUN_000f76f4` / `FUN_000f68e0`
  read the pad through `sceCtrl` (`FUN_0036da4c`) and feed a libpad-style layer.

So the input path is understood up to a **generic pad library**; the button **consumers** -- the code
that turns a d-pad press into a selection change -- are the remaining target. That is a bounded
code-tracing task, and it is the natural point to bring in a second agent: the question is
well-specified ("which function writes the value that selects the render block at `0x09DEE480`?"),
the artifacts are on disk, and the answer is verifiable against the live game.

### Method note

> **Follow a candidate pointer all the way before calling it the target.** The alternating pointer
> looked like a cursor for one section. Following it two levels showed quad records and a glyph chain,
> which identifies it as rendering with no further speculation. Two reads settled a question that more
> period analysis could not -- because period analysis cannot distinguish an index from a derived value,
> but *content* can.



---

## 42. The render-array PRODUCER is identified and verified against the measured layout

Section 41 concluded the `0x09DEE480` array is render state and that the remaining work is a code
trace. A second agent (Codex) was given the decompilation artifacts with a single bounded question --
*which function writes the value that selects the render block?* -- and its primary candidate has now
been **verified against the measurements**.

### Codex's answer (recorded verbatim in principle)

* **`FUN_0025595c`** (Ghidra `0x0025595C`, RAM `0x08A5995C`) -- best concrete render-block writer
  candidate, called once per drawable child by `FUN_0024aee0` immediately before the loop that reads
  block `n` at `base + n * 0x50` via renderer `+0x14`.
* **`FUN_0024aee0`** -- the confirmed **consumer** (the draw callback), with the render-array pointer at
  renderer `+0x14`, block count at `+0x18`, primitive count at `+0x1C`.
* **`FUN_00255170`** -- secondary, for nodes lacking the normal drawable at node `+0x10`.
* And, importantly, Codex **declined to claim** a selection-index writer: *"The actual logical selection
  index writer cannot be identified honestly from the supplied bodies."*

### The verification

Decompiling `FUN_0025595c` (size **2108**, **32 stores**) shows the block population directly:

```c
if (*(int *)(DAT_00397770 + 0x18) < 0xaa) {                    // count guard, max 0xAA
    puVar12 = (undefined4 *)(*(int *)(DAT_00397770 + 0x14)     // array BASE
                             + *(int *)(DAT_00397770 + 0x18) * 0x50);   // COUNT * stride
    *(int *)(DAT_00397770 + 0x18) = *(int *)(DAT_00397770 + 0x18) + 1;
} else puVar12 = 0;
...
*puVar12     = pbVar5;    // +0x00
puVar12[1]   = psVar4;    // +0x04
puVar12[2]   = param_5;   // +0x08
puVar12[3]   = fVar15;    // +0x0C
```

**Every element of that matches what section 41 measured in RAM:**

| prediction from the decompile | measurement from the live game |
|---|---|
| stride `0x50` | array of 13 blocks at stride `0x50` |
| three leading pointers at `+0x00/+0x04/+0x08` | three pointers at block `+0x30/+0x34/+0x38` (same triple, block-relative `+0x00/+0x04/+0x08`) |
| the first two are pointers into geometry | alternating pointers to quad/display-list records |
| count lives beside the base | base/count read from the object `FUN_0025595c` uses |

So the code that **writes** the measured array is identified, and the identification is corroborated by
an independent layout match rather than by assertion. This is the first end-to-end verified link between
a decompiled routine and a measured RAM structure in this investigation.

### Also established this section

The class-registration table in `FUN_000fa84c` (6,856 bytes) constructs the singletons:

```c
if (DAT_0132fdb0 == 0) { DAT_0132fdb0 = 1; FUN_000f7598(&DAT_0132fb40);
                         FUN_0033c4d0(&DAT_00392bb8); }
DAT_003925b0 = &DAT_0132fb40;      // the PAD OBJECT, ctor FUN_000f7598
...
if (iRam0006c080 == 0) { iRam0006c080 = 1; FUN_0024aa28(0x5e650); ... }
DAT_00397770 = 0x5e650;            // a menu-class handle
```

A **query on the intersection of interest** produced this: listing every function referencing the menu
static `DAT_00397770` and every function referencing the pad object `DAT_003925b0` yields exactly
**2** functions -- the boot initialiser `FUN_002dbf50` (already analysed) and this registration routine.
So there is no single small "menu update" function that touches both statics directly; the menu polls
input through a layer, which is consistent with section 41's finding that the input path dead-ends in a
generic pad library.

### Honest status

* **Verified:** the producer of the render array (`FUN_0025595c`), the consumer (`FUN_0024aee0`), the
  array base/count fields, and the class-registration table.
* **Not found:** the **logical selection index** -- the value that decides *which* row is rendered
  highlighted. It is not written by anything directly reachable from the mapped input path, and no
  function touches both the menu static and the pad object except the two initialisers.
* **Why the next step is different in kind:** the selection must be computed by a routine that reads pad
  state through the pad *layer* rather than the static. Finding it needs either (a) a write watchpoint on
  the render array to catch the caller in the act -- if the debugger supports breakpoints -- or (b) a
  search for readers of the **pad layer's** output field rather than the pad object itself. Both are
  concrete and bounded; neither is more RAM scanning.

### Method note

> **Give a second agent a single bounded question and the artifacts, then verify its answer yourself.**
> Codex produced a ranked candidate list *and* an explicit refusal to over-claim the index -- which was
> correct and more useful than a confident guess. The verification step (does its predicted layout match
> the bytes I measured?) is what converts its answer from a suggestion into a result. Delegation is
> valuable here exactly because the question was well-specified; the earlier open-ended phases were not
> delegable.



---

## 43. PPSSPP DOES support write watchpoints — and the first attempt froze the game

Section 42's next step was a write watchpoint. This section records the capability, the address-validity
problem, and a self-inflicted failure worth knowing about.

### The capability exists

Probing the debugger protocol found working breakpoint events:

```
memory.breakpoint.add  {address, size, type}     -> accepted ("size" is REQUIRED)
memory.breakpoint.clear.all                      -> accepted
cpu.breakpoint.add                               -> accepted
```

and the game **logged a hit** on the first probe with no breakpoint even intended:

```
CHK Write128(CPU) at 09dee480 ((09dee480)), PC=08...
```

So PPSSPP **does** report the writing PC for a watched memory address -- which is exactly the mechanism
needed to name the code that writes the render block. That capability is now established and scripted
(`scripts/psp-watch-render.py`, `psp-watch-render2.py`).

### Two problems, both real

**1. The watched address is only valid for one screen instance.** The render-array base is
`*(int *)(DAT_00397770 + 0x14)`, i.e. a field *inside the manager object*. Since the manager is
heap-allocated per screen (section 39), the array moves between screens -- so `0x09DEE480` was the
pause-menu instance's base and is **not** where the title screen renders. With the game at the title
screen, a watchpoint on `0x09DEE480` produced **zero hits** while the CPU was confirmed free-running
(`ticks delta 781,501,783`, `stepping: False`). The correct watch target is therefore **the manager's
base/count fields** (or a freshly resolved base per screen), not a remembered data address.

**2. Debugger churn FROZE the game, and that looks exactly like "the address is not written".**
After a series of `cpu.resume` calls, breakpoint add/clear cycles and repeated probes, the game halted:
`ticks delta 0` over 2.5 s, `pc` pinned at `0x08B5A6C0`, `stepping: true`, and a **screen diff of
exactly 0 pixels** across a button press. The watchpoint then appeared never to fire -- but the truth was
that *nothing was executing*. A frozen game and an unwritten address produce the same reading, and only
the **ticks delta** distinguishes them.

Recovery: kill PPSSPP and relaunch (`Stop-Process -Force`, verify the process count is 1). The game
returned to the title screen and `ticks delta 781,501,783` confirmed normal execution.

### What this leaves

* **Capability proven and scripted** -- a write watchpoint can name the writing PC. This is the direct
  route to the remaining question and it does not need more RAM scanning.
* **Correct target identified** -- watch the manager's **base/count fields**, or resolve the array base
  fresh for whatever screen is on, rather than reusing an address measured on a different screen.
* **A new failure mode logged** -- debugger churn freezes the emulator, and a frozen emulator is
  indistinguishable from a null result unless the ticks delta is checked every time. Any watchpoint run
  must assert `ticks` advances *before* trusting a zero-hit outcome.

### Method note

> **Check the CPU is executing before believing a watchpoint's silence.** "No hit" has two very
> different causes -- the address was not written, or nothing ran -- and only a ticks delta separates
> them. This is the same class as every other instrument fault in this investigation: a broken
> instrument and a true negative look identical unless the instrument's liveness is asserted
> independently.

> **Do not watch a data address remembered from a previous screen.** When an object is heap-allocated per
> screen, its internal arrays move. Watch the field that *points* to the array, or re-resolve the base on
> the screen in front of you.



---

## 44. The manager object IS mapped — and a second frozen-emulator false negative

Section 43 concluded the watch target should be the *field that points to* the render array. Finding it
produced the fullest map of the manager so far, plus a repeat of the instrument fault that section 43
had just diagnosed.

### The manager, fully mapped

`DAT_00397770 + 0` holds `0x08C08EB0`; the array base `0x09DEE3C0` occurs **exactly once** in all
readable RAM — at `0x08C08EC4`, i.e. **manager `+0x14`**, the field the decompiled writer reads:

```
manager +0x00 = 0x09DEE380        +0x18 = 3            <- render block COUNT (writer's field)
manager +0x04 = 0x09DF18F0        +0x1C = 12
manager +0x08 = 0x09DEE380        +0x20 = -1
manager +0x0C = 0x09A3F000        +0x24 = 1            <- small ordinal
manager +0x10 = 0                +0x28 = 0x08C09344
manager +0x14 = 0x09DEE3C0   <-- RENDER ARRAY BASE   +0x2C = 0x08C09210
```

Two things worth noting:

* **`+0x14` = the render base and `+0x18` = the count** are confirmed as *live fields of this object* —
  this is precisely the target section 43 said to watch (the field, not the remembered array address).
* **`+0x28 = 0x08C09344` / `+0x2C = 0x08C09210`** are the head/tail of the 35-node list walked in
  section 39. So the same object carries the render base, the node list, the `-1` sentinel and the
  count fields `3` and `1`.

### The false negative, twice

Polling `+0x14`, `+0x18`, `+0x1C`, `+0x20`, `+0x24`, `+0x28`, `+0x2C` across six `down` presses showed
**no field moved**. The delivery check then reported the reason:

```
display change on the last press: 0 px
ticks delta 2.5s: 0   stepping=True   paused=False   (screen blank, lum 237)
```

**The emulator was frozen again.** Probing the debugger protocol — repeated `cpu.resume`, breakpoint
add/clear cycles, many short-lived connections — halts PPSSPP with `stepping: true` and `ticks` frozen,
and a halted emulator produces *exactly* the readings a true negative produces: 0-pixel diffs, no
watchpoint hits, unmoving fields.

This is the second occurrence in two sections, so it is now a **standing precondition** rather than a
one-off:

> **Assert that the emulator is EXECUTING (ticks advancing) before AND after any observation run.**
> If `ticks` does not advance, no conclusion is valid. Recovery that works reliably is kill-and-relaunch
> (`Stop-Process -Force`, verify the process count is 1); clearing breakpoints plus `cpu.resume` does
> **not** reliably clear the frozen `stepping` state.

`scripts/psp-live-check.py` implements this check with an optional `--recover` attempt, so future runs
fail loudly instead of returning a plausible-looking null.

### Where the cursor hunt actually stands

The manager object is now **completely mapped**, and `+0x24 = 1` is a small ordinal sitting beside the
render base and the list pointers — the most cursor-shaped field found in this object so far. But it
**cannot be evaluated from the run above**, because nothing was executing. It needs a clean run with:

1. `psp-live-check.py` confirming `ticks` advances;
2. two connections (reads vs input);
3. presses verified to move the display;
4. **a menu with more than two rows**, since section 40's rule stands: a 2-item list cannot distinguish
   an index from derived state.

### Method note

> **An instrument that freezes produces true-looking negatives.** This is the fourth distinct instrument
> fault in this investigation (silent capture crash; coarse grid erasing a highlight; reads suppressing
> presses; now a halted CPU) and every one of them yielded a *confident wrong answer* rather than an
> error. The countermeasure that works is not care but **assertion**: measure a quantity that must be
> non-zero if the system is alive — here, the CPU tick delta — and refuse to interpret anything
> otherwise.



---

## 45. The manager ordinal is ruled out — by a run that was actually valid

Section 44 could not evaluate the manager's `+0x24 = 1` because the emulator was frozen. This section
does it properly, and the result is a **trustworthy negative**.

### The run satisfied every precondition

```
liveness BEFORE   ticks delta 1.5s: 666,666,000   stepping=False -> EXECUTING
liveness AFTER    ticks delta 1.5s: 674,073,400   stepping=False -> EXECUTING
presses that moved the display: 12/12
manager for this screen: 0x08C08EB0
```

Three independent liveness signals agree: the CPU advanced before and after, **every one of 12 presses
moved the display**, and two connections were used (reads vs input). So a null here is a fact about the
data, not about the instrument -- which is exactly what sections 43 and 44 could not say.

### The result

```
press        +0x14      +0x18      +0x1C      +0x20      +0x24      +0x28      +0x2C
    0    165602240          0          0         -1          1  146838604  146838516
    1    165602240          0          0         -1          1  146838604  146838516
  ...                        (identical through press 12)

=== fields that moved ===
   (none)
```

**Every field of the manager object is static across 12 verified presses**, including `+0x24 = 1`, the
small ordinal that was the most cursor-shaped field in the object.

### What this rules out, and the refinement it forces

**Ruled out:** the per-screen manager's header fields -- the render base, the block count, the `-1`
sentinel, the `+0x24` ordinal, and the node-list head/tail. Note the header changed *between screens*
(`+0x18` was `3` on the pause menu and `0` here), so it is screen state, but it is **not** selection
state.

**The refinement:** the highlighted row is **not stored in the manager object**, so it must live in one
of the objects the manager *points at*. The manager's pointers are exactly:

| field | points to | status |
|---|---|---|
| `+0x00` | `0x09DEE380` (render-related) | unexplored |
| `+0x04` | `0x09DF18F0` | unexplored |
| `+0x0C` | `0x09A3F000` | unexplored |
| `+0x14` | render-node array `0x09DEE3C0` | identified as **derived** render state (s41) |
| `+0x28` / `+0x2C` | the 35-node list | **static** (s39) |

So the search now has a **small, enumerated list of targets** rather than a 24 MB span: follow
`+0x00`, `+0x04` and `+0x0C` and look for a wrapping ordinal in each. That is three bounded reads, not
another scan.

### Why this section is worth recording despite being a negative

Because it is the **first trustworthy negative in the cursor hunt**. Sections 40, 41, 43 and 44 each
produced results that had to be withdrawn or re-qualified because of an instrument fault. This one has
liveness asserted on both ends, delivery verified on every press, and connections separated -- so
"the manager header is not the cursor" can now be relied upon, and it narrows the search to three
pointers instead of clearing nothing.

### Method note

> **A negative is only as good as the run that produced it.** The same test, run twice, gives
> "no field moved" both times -- but on a frozen emulator that means nothing and on a verified one it
> means the field is not the cursor. Assert liveness *and* delivery, then the negative becomes load
> -bearing. The earlier sections' negatives were not wrong so much as unsupported; this one is
> supported, and it moves the target.



---

## 46. Two more objects cleared with the verified harness — and the pattern that emerges

Section 45's next step was to follow the manager's three unexplored pointers using the same
verified-precondition harness. Two are now done.

### The manager's pointers, resolved

```
manager +0x00 -> 0x09DEE380     struct with small ordinals (4, 4, 28, 4) and pointers
manager +0x04 -> 0x09DF18F0     all zeros -- a buffer, deprioritised
manager +0x08 -> 0x09DEE380     (same as +0x00)
manager +0x0C -> 0x00000000     not a pointer on this screen
manager +0x14 -> 0x09DEE3C0     render-node array (derived render state, s41)
manager +0x28 -> 0x08C094D0     node-list head  (s39)
manager +0x2C -> 0x08C09210     node-list tail
```

### Result: `0x09DEE380` is static, verified

```
liveness BEFORE   ticks delta: 666,666,000   stepping=False -> EXECUTING
presses that moved the display: 12/12
=== words that moved ===  (none)
=== wrap test ===          (nothing to test)
liveness AFTER    ticks delta: 666,666,000   stepping=False -> EXECUTING

VERDICT: TRUSTWORTHY NEGATIVE -- nothing in this struct tracks the highlight.
```

So the struct carrying the small ordinals (`4`, `4`, `28`, `4`) does **not** track the highlight. Those
ordinals are counts, and the object is stable while the selection changes.

### The pattern worth recording: the manager is a SCREEN object, not a selection object

Note that the manager's fields *do* differ between screens -- `+0x18` was `3` on the pause menu and `0`
here, and `+0x28`/`+0x2C` differ too. So the manager genuinely is per-screen state. But across 24
verified presses on two screens, **no field of the manager and no field of its `+0x00` target moves with
the highlight.**

That is a consistent, twice-confirmed finding:

> **The manager records what the screen IS (its base, counts, node list) and not what is SELECTED.**
> The selection is held elsewhere, and it is not in the object graph reachable by following the
> manager's own pointer fields at these offsets.

Combined with sections 40/41 (the render array is *derived* from the selection, not the selection), the
remaining structural possibility is narrow: the selection lives in the **node list itself** (the 35 nodes
at `+0x28`), which section 39 measured as static -- but on a *different* screen, and with **no
liveness assertion**. That measurement is therefore of exactly the class that sections 43-45 showed
cannot be trusted, and it should be **re-run with the verified harness**.

### What to do next, precisely

1. **Re-run the node-list walk with the verified harness.** Section 39's "35 nodes, no field changed" is
   the last unverified negative in the hunt and the nodes are the natural home for a per-item
   highlight flag. This is the single highest-value remaining test.
2. If the nodes are static under a *verified* run, then the selection is **not in the manager's object
   graph at all**, which would be a strong, defensible conclusion rather than another cleared address.

### Method note

> **Re-run old negatives with the new harness.** Every negative taken before the liveness precondition
> existed is suspect -- the fault is silent, so a null measured on a frozen emulator is indistinguishable
> from a real one. Rather than treating earlier clears as settled, the cheap and correct move is to
> re-measure the *structural* ones (especially the node list, which is where a per-item flag would
> naturally live) with the harness that can now certify a null. A list of unverified negatives is not a
> narrowed search.



---

## 47. A FIFTH instrument fault: the delivery check passes trivially on an animating scene

Section 46 nominated the node-list walk as the decisive test, and it was run under the verified harness.
It returned a clean-looking result:

```
liveness BEFORE   ticks delta: EXECUTING
head/tail/count: (146838868, 146838780, 11)   nodes walked: 11
presses that moved the display: 12/12
node counts per sample: [11 x13]
node ADDRESSES identical across samples: True
=== node fields that changed (compared by index) ===   (no node field changed)
liveness AFTER    ticks delta: 666,611,252   stepping=False -> EXECUTING

VERDICT: TRUSTWORTHY NEGATIVE -- the node list does not track the highlight,
         so the selection is NOT in the manager's object graph.
```

**That verdict is void**, and the check that voids it took one command:

```
frame diff with NO press: 1,575,031 px -> ANIMATING (scene!)
```

The run was on an **animating 3D scene**, not a static menu.

### The fault

The harness's delivery check accepts a press as delivered when the display changed. On an animating
scene the display changes continuously, so **every press passes trivially** -- `12/12` delivery told me
nothing at all. The same trap then hides in the verdict logic: "presses moved the display" was supposed
to certify that the *highlight* moved.

This is the **fifth distinct instrument fault** in this investigation:

| # | fault | how it produced a confident wrong answer |
|---|---|---|
| 1 | screenshot child died (`ModuleNotFoundError: PIL`) | "no change" from a crashed capture |
| 2 | coarse 8x8 grid hash | erased a moving highlight; "menu does not scroll" |
| 3 | reads on the input connection | suppressed presses to 1/30th effect |
| 4 | debugger churn halts the CPU | frozen emulator reads exactly like a true negative |
| 5 | **animating scene + per-press movement check** | **delivery reads 12/12 while measuring nothing** |

All five share one shape: **the instrument's failure is indistinguishable from a real result.** And the
common countermeasure is the same each time -- measure a quantity whose value pins down the instrument's
liveness, and refuse to interpret otherwise.

### The guard, now implemented

`psp-probe-nodes.py` gains an **animation guard** between the liveness check and the walk:

```python
g1 = grab(); sleep(2.5); g2 = grab()
base_diff = pixdiff(g1, g2)
if base_diff > 2000:
    REFUSE: the display animates on its own (a scene, not a menu), so a per-press movement
            check cannot distinguish a highlight move from ordinary motion.
            Reach a STATIC screen (two-frame diff ~0) before running this test.
```

So a run now requires **three** preconditions, each measured rather than assumed: ticks advancing
(liveness), a static screen (animation guard), and a per-press change (delivery).

### What survives from this run

Only the structural observation, which does not depend on the delivery check:

* the manager's node count on this screen is **11**, not the 35 measured in section 39 -- so the list is
  **per-screen**, confirming the manager is rebuilt per screen;
* the node addresses were **identical across all 13 samples** and the count never varied, so the list is
  stable while the game runs.

The node-field negative is **not** established. The decisive node test still has to be run, on a
**static** screen, with the animation guard in place.

### Method note

> **A delivery check must be able to FAIL.** Counting any per-press movement certifies nothing when the
> thing being measured moves on its own; the check has to be calibrated against a no-press baseline
> first. This is the same discipline as the no-press control for memory (Rule 120) applied to the screen:
> **measure the baseline, then require the press to exceed it.** A guard that cannot fail is not a guard.

> **Re-verify the target's identity before trusting a null.** The run was clean, verified, and
> self-consistent -- and was measuring a scene. Sections 43-47 have now had five faults of this kind; the
> cost of one extra two-frame capture before each run is trivial next to a voided result.



---

## 48. The harness now works as designed — it refused twice instead of lying

Section 47 implemented the animation guard. This section records the first runs where the harness
**correctly refused** rather than producing another void "trustworthy" verdict. Two refusals, both
accurate, are the useful result.

### All three preconditions, measured separately

Before the node test, the screen and CPU were verified by hand:

```
liveness:            ticks delta 888,888,000   stepping=False  -> EXECUTING
no-press frame diff: 0 px                                     -> STATIC (menu)
reached via 'l'      (STATIC+UI, sat 10.71%)
```

So both the CPU and the screen were correct at that moment.

### Refusal 1 — the animation guard fired

Running the probe immediately afterwards:

```
=== liveness BEFORE ===
  before  ticks delta: 666,700,980  stepping=False -> EXECUTING
  no-press frame diff: 1,330,967 px
REFUSING: the display is ANIMATING on its own (a scene, not a menu), so a per-press
movement check cannot distinguish a highlight move from ordinary motion.
rc=5
```

The game had returned to a scene in the seconds between the manual check and the probe. **The guard
caught it and returned a distinct exit code (5) instead of a verdict** -- which is exactly the behaviour
section 47 asked for and which no previous run had.

### Refusal 2 — the delivery check fired on a genuinely static screen

Chaining "reach a static screen" and "run the probe" into one process removed the gap, and the guard
then passed on a real menu:

```
  no-press frame diff: 0 px
  -> STATIC screen confirmed; a per-press diff is now meaningful.
manager 0x08C08EB0
  head/tail/count: (146839792, 146838032, 9)   nodes walked: 9
=== presses that moved the display: 0/12 ===
node counts per sample: [9 x13]
node ADDRESSES identical across samples: True
=== node fields that changed ===   (no node field changed)
=== liveness AFTER ===
  after   ticks delta: 666,666,000   stepping=False -> EXECUTING
VERDICT: VOID -- no press moved the display; readings are not evidence.
```

**This is the correct outcome.** On a screen confirmed static (0 px with no press) and a CPU confirmed
executing, `down` moved **nothing** -- so the field readings above it are not evidence either way, and
the harness says so instead of reporting "trustworthy negative".

### What the two refusals establish

* **The animation guard works**: it distinguishes a scene from a menu by measurement, not assumption, and
  refuses with a distinct code.
* **The delivery check now has teeth**: because the screen is verified static first, `0/12` is now
  *informative* -- it means `down` genuinely did nothing on this screen. Previously the same `12/12`
  certified nothing.
* **The harness has become a filter, not a recorder.** Both runs were refused, and a refusal costs one
  run rather than a withdrawn section.

### What is still not established

**The node-field negative.** The list walked cleanly (9 nodes on this screen; 11 and 35 on the other two
screens seen, so the list length is definitively per-screen) and every node address was identical across
13 samples -- but **no press moved the highlight**, so the node fields were never exercised. The
decisive test still needs a static menu whose highlight actually moves.

Practical requirement for the next attempt: find a static UI screen where a **d-pad direction actually
moves the selection**. The earlier pause menu scrolled with `down` (verified twice by OCR of the changed
panel), so the immediate next step is to return to *that* screen and re-run, rather than accepting a
screen where `down` is inert.

### Method note

> **A refusal with a distinct exit code is a result.** Sections 43-47 each produced a confident verdict
> that later had to be withdrawn; this section produced two refusals and no verdict, which is strictly
> better -- the runs cost minutes and no part of the document had to be corrected. Building the guard
> was worth more than any single measurement it protected, because it applies to every future run.
>
> **Distinguish "no press moved the display" from "the field is not the cursor."** Those are different
> claims and only the first is supported here. The harness now separates them, which is why this
> section can be honest about not having the answer.



---

## 49. The guards hold on a third screen — and the blocker is now a SCREEN, not the method

Section 48's next step was to return to the pause menu (verified twice by OCR to scroll with `down`)
and re-run the node test. The chained script did everything right, and the guards caught the problem
before any wrong conclusion was drawn.

### The chained run, step by step

```
=== 1. open the pause menu (start) ===
=== 2. static check (no press) ===
   no-press diff: 0 px -> STATIC
=== 3. does 'down' move the highlight here? ===
   down-press diff: 0 px -> NO MOVEMENT
```

**Step 3 is the informative one.** The screen is genuinely static (`0 px` with no press) but `down`
moves nothing, so this is **not** the pause menu -- `start` landed somewhere else. The node probe then
re-verified and voided:

```
  no-press frame diff: 0 px  -> STATIC screen confirmed
head/tail/count: (146840056, 146838032, 9)   nodes walked: 9
=== presses that moved the display: 0/12 ===
node counts per sample: [9 x13] ; node ADDRESSES identical
=== node fields that changed ===   (no node field changed)
=== liveness AFTER ===  EXECUTING
VERDICT: VOID -- no press moved the display; readings are not evidence.
```

**A third screen, a third list length.** The node count has now been measured on three screens:
**35** (section 39), **11** (section 47), **9** (here). The manager and its list are definitively
**rebuilt per screen**, which is consistent with every other per-screen finding and confirms the
manager is not a persistent object.

### What this establishes about the method

The harness has now produced **four correct refusals in a row** (sections 47, 48 x2, 49) and no void
verdict has been mistaken for a result. The guards are doing exactly their job:

| guard | what it caught here |
|---|---|
| liveness | emulator executing (passed) |
| animation guard | no-press diff `0 px` -> static (passed) |
| delivery check | `0/12` -> **void** (fired correctly) |

So the run cost minutes and **nothing in the document needed correcting** -- a marked improvement over
sections 43-47, each of which produced a verdict that later had to be withdrawn.

### The blocker, stated precisely

**The blocker is now a screen, not a method.** Everything except the target screen is in place:

* the harness refuses invalid runs;
* the manager object is located and mapped, and its list walks correctly;
* every per-screen object that has been measured is either explained or verified-static;
* the code side has the render producer and consumer identified and verified.

What is missing is a **static UI screen on which a d-pad direction actually moves the highlight**.
Blind input cannot reliably reach one: `start` here opened an inert screen, and the pause menu is
reached only from a specific state. This is precisely the point where a sighted helper is worth more
than another automated attempt -- the automated side can *verify* a screen in seconds, but it cannot
*choose* one.

### What to ask for

Reaching the **title menu** (Story / Battle / Customize / Museum / Shop / Options / Data) settles two
things at once:

1. it is a **static** screen, so the animation guard passes;
2. it has **seven rows**, so a real selection index would show **period 7 and step 1** -- which satisfies
   section 40's rule that a two-item menu cannot distinguish an index from derived state.

With the highlight on *Story*, the node probe and `psp-probe-struct.py` can both be run immediately, and
the node-field question -- the last unverified structural negative -- can finally be answered.

### Method note

> **Verify before blaming the method.** Four consecutive refusals show the instrumentation is sound; the
> same four refusals also show that the *reachability* of a suitable screen is the binding constraint.
> When a well-instrumented pipeline keeps refusing, the next move is to change the input condition (here,
> the screen) rather than to refine the instrument again -- and to say so plainly instead of running the
> same test on another inert screen.



---

## 50. MEASURED: blind input cannot reach a scrollable screen — 7 screens, 6 controls, zero movement

Section 49 concluded the blocker was reachability of a suitable screen and proposed asking a sighted
helper. Rather than assume that, the claim was **tested systematically**, and it holds: an automated
search over seven screens, pressing six controls on each, found **no static screen whose highlight
moves**.

### The search

`psp-find-scroll.py` walks back through screens. On each it:

1. confirms the CPU is executing (ticks delta);
2. confirms the screen is **static** (two captures, no press, difference ≤ 2000 px);
3. on a static screen, presses each of `down, up, right, left, l, r` and records whether the display
   changed **and** stayed static afterwards;
4. otherwise navigates back (`circle`, `start`) and repeats.

### The result

```
screen 0..6:  no-press diff 0 px -> STATIC
     down  -> no movement        up    -> no movement        right -> no movement
     left  -> no movement        l     -> no movement        r     -> no movement
     no control moves the highlight on this screen; navigating back

no scrolling static screen found in 7 steps.
```

**Seven consecutive screens, every one static, every one inert to all six controls.** Not one produced
a highlight movement.

### What this establishes

* **The instrumentation is not the problem.** All seven screens passed the liveness and staticness
  guards, so the search was valid on each. The zero-movement readings are therefore real: those screens
  genuinely do not respond to those controls.
* **Blind input has a measured ceiling.** The game's reachable screens from this state are, on the
  evidence, either static-and-inert or animating. Whatever screens DO scroll (the pause menu was
  previously verified twice by OCR to scroll with `down`) are not reachable by blind navigation from
  here.
* **The blocker is now a fact, not an inference.** Section 49 said "blind input cannot reliably reach
  one"; this section shows it cannot, over seven attempts with six controls each.

### Consequence for the cursor hunt

The remaining unverified structural question -- **do the manager's node fields track the highlight?** --
cannot be answered by more automated attempts from this state. The instrument is ready and would answer
in one run:

* `psp-probe-nodes.py` verifies liveness, staticness and delivery, and voids itself otherwise;
* it needs only a **static screen where one control moves the selection**.

The exact, minimal ask is therefore: **place the game on a static menu with a list of several rows --
the title menu is ideal -- and tell me which button moves the highlight.** With that, one run settles
the node-field question.

### Method note

> **Test a reachability claim before delegating on it.** "I cannot get to the right screen" is easy to
> assert and easy to be wrong about, so it was measured: seven screens, six controls, zero movements,
> with the guards confirming each screen was valid. A measured reachability ceiling is a legitimate
> stopping point for the automated phase and a precise handoff -- it tells the helper exactly what is
> needed (a *scrolling* static menu) rather than "help me with menus".

> **Seven systematic negatives in a row are worth more than another improvised attempt.** Each of the
> seven screens was checked for liveness and staticness first, so the series is evidence rather than
> seven guesses. When a search is exhausted *with the guards in place*, the right move is to change the
> input condition -- not to re-run with a different button order.



---

## 51. The manager header is a SCREEN CLASSIFIER — and `render count` distinguishes interactive screens

Section 50 established that seven reachable screens are static and inert. While probing the watchpoint, a
correlation appeared that is worth recording, because it turns screen selection from blind pressing into
a **measurement**.

### The observation

On the screen where the watchpoint silently did nothing, the manager's **render block count was 0**:

```
manager 0x08C08EB0   render base 0x09DEE3C0   render COUNT 0
```

A render count of 0 means **no render blocks are populated**. The manager is still present and valid --
its node list walked fine -- but it is not building any drawable blocks. That is the signature of a screen
that draws nothing interactive.

### The census

`psp-manager-census.py` reads the manager header and the node-list length, then presses a control and
re-reads:

```
liveness: EXECUTING
manager 0x08C08EB0
  render base 0x09DEE3C0  render COUNT 0  +0x1C 0  +0x20 -1  +0x24 1  node COUNT 11
  nodes walked from head: 11
  no-press frame diff: 1,295,647 px -> ANIMATING

press  render  f1C  f20  f24  nodes    display diff
    1        0    0   -1    1    11     1,049,960 px
    2        0    0   -1    1    11     1,692,119 px
    3        0    0   -1    1    11     1,508,390 px
    4        0    0   -1    1    11     1,664,256 px
```

Two things follow:

* **The manager is a per-frame object, not a menu object.** It exists and is well-formed on an animating
  3D scene (`node COUNT 11`, `+0x20 = -1`, `+0x24 = 1`, a walkable 11-node list) with a render count of
  0. So the object is maintained continuously and is not itself "the menu".
* **`render count` is a candidate screen classifier**: `0` on a scene that draws no blocks, and `> 0`
   on the pause menu where the render array held populated blocks (sections 41/44 measured 3 and 13
  blocks there).

### What this changes

The screen search in section 50 pressed six controls on seven screens and found nothing. If
**`render count > 0`** reliably marks an interactive screen, then the search becomes a **read**, not a
press: poll the manager's `+0x18` and look for a nonzero value, which costs one 64-byte read per candidate
state instead of six button presses plus twelve screenshots.

That does not by itself reach such a screen, but it makes recognising one trivial -- and it means the
final step needs a helper only to *land* on an interactive screen, not to identify one.

### Honest status

* **Established:** the manager persists across screens and scene states; its render count is `0` on a
  non-drawing screen and nonzero on the pause menu; node count varies per screen (9 / 11 / 35).
* **Not established:** that `render count > 0` is a *reliable* classifier -- that needs sampling several
  screens rather than one. The correlation is suggestive and cheap to test, and it is the natural next
  measurement.
* **Unchanged:** the node-field question is still open, and still needs a static screen on which a control
  moves the highlight.

### Method note

> **Look for a cheap read that replaces an expensive search.** Six controls x N screenshots per screen is
> a costly way to classify a screen; if a single 64-byte read of a manager field encodes "is this screen
> drawing blocks", the search collapses to a poll. Whenever a probe is expensive, check whether some
> already-located structure carries the same information as a scalar.



---

## 52. RETRACTION: the manager header does NOT classify screens — `render count` and `+0x20` both fail

Section 51 proposed `manager +0x18` (render block count) as a screen classifier, and the `+0x20`
sentinel as a second candidate. Both are now **falsified** by comparing two screen states directly.

### The sample table

| screen state | render `+0x18` | `+0x1C` | `+0x20` | `+0x24` | node COUNT |
|---|---|---|---|---|---|
| animating 3D scene (fresh title) | **0** | 0 | **-1** | 1 | 11 |
| static UI screen (reached via `l`) | **0** | 0 | **-1** | 1 | 11 |
| frozen / halted emulator | 1 | 0 | 1 | 1 | 9 |
| pause menu (sections 41/44) | 3 | 12 | -1 | 1 | 35 |

The two states that matter — an **animating scene** and a **static UI screen** — produce **byte-identical
headers**:

```
render COUNT 0   +0x1C 0   +0x20 -1   +0x24 1   node COUNT 11
```

So neither `render count` nor `+0x20` distinguishes them. The apparent correlation in section 51 came
from comparing a *scene* (0) against the *pause menu* (3) — but the static UI screen also reads 0, and
that is precisely the screen type the classifier needed to find.

### What is actually true

* **The manager is a persistent global object, not a per-screen one.** It is present and structurally
  identical across an animating scene and a static UI screen. The earlier per-screen "differences"
  (node counts 9 / 11 / 35) are better explained by a **halted emulator** (9, where the state is not a
  screen at all) and by the pause menu genuinely having more nodes (35).
* **No cheap manager-side classifier exists** among the fields measured. Screen type has to be
  determined from the display, as `psp-reach-menu.py` already does reliably (two-frame diff plus
  saturated-colour percentage).
* The `frozen` row is a useful reminder that a halted emulator's header is **not** a screen's header —
  it is a fourth state that would silently pollute any classifier trained on it.

### Consequence

Section 51's plan to replace an expensive screen search with a single 64-byte read **does not work**;
the measurement it was based on conflated two screen types. The screen search must continue to use the
display-based test, which is slower but correct:

* `psp-reach-menu.py` — two-frame diff plus colour saturation, which has correctly identified static UI
  screens repeatedly ("STATIC+UI <-- MENU via 'l'");
* and the scrollability test (`psp-find-scroll.py`), which presses each control and requires the display
  to change **and stay static**.

### Method note

> **A classifier must be tested on the pair of classes it is meant to separate.** `render count = 0`
> looked like a scene-vs-menu discriminator because the comparison in section 51 was scene (0) against
> the pause menu (3). Adding one more sample — a *static UI screen* — showed it also reads 0, i.e. the
> value is **constant across the two classes that matter**. Two samples can support a hypothesis that
> three refute; the third sample is what makes the test meaningful.

> **Watch for a fourth state.** A halted emulator produced a header that fits no screen category
> (`render 1, +0x20 1, nodes 9`). Any classifier built without asserting liveness would have learned
> from that state as if it were a screen.



---

## 53. FACE BUTTONS found — but the reachable static screen still cannot be exercised

Two attempts this section, both guarded, one encouraging and one another void.

### Encouraging: `circle` and `triangle` DO move a static screen

A hunt for a static screen, then a scan of all twelve controls **on the verified static screen**
(`no-press diff 0 px`):

```
=== scanning all controls ON this verified static screen ===
  down      ->         0 px
  up        ->         0 px
  left      ->         0 px
  right     ->         0 px
  l         ->         0 px
  r         ->         0 px
  circle    ->   1529922 px  <== MOVED (still static -> SELECTION MOVED)
  cross     ->   1649726 px  <== MOVED (not static after -> scene motion)
  triangle  ->    388654 px  <== MOVED (still static -> SELECTION MOVED)
  square    ->         0 px
  start     ->         0 px
  select    ->         0 px
```

The distinction the scan draws is the important part. `circle` and `triangle` changed the display **and
the screen was still static afterwards** -- which is the signature of a **UI transition**, not scene
motion. `cross` changed it but did *not* stay static, which is scene motion. Earlier scans that tested
only six controls (`down/up/left/right/l/r`) found nothing; adding the face buttons revealed two live
controls.

So the screen the investigation had been calling "inert" is **not** inert -- it responds to `circle` and
`triangle`. Previous refusals were partly a **coverage gap in the control list**, not only a screen
problem.

### Void: the same screen does not respond to `triangle` when re-entered

Reaching the screen via `l` and pressing `triangle` eight times:

```
=== liveness BEFORE === EXECUTING
head/tail/count: (146838736, 146838032, 9)   nodes walked: 9
=== presses that moved the display: 0/8 ===
node counts per sample: [9 x9] ; node ADDRESSES identical
=== node fields that changed ===   (no node field changed)
=== liveness AFTER === EXECUTING
VERDICT: VOID -- no press moved the display; readings are not evidence.
```

`0/8` delivery, so the node result is void again. The screen reached by `l` here is a *different* one
from the screen where `circle`/`triangle` moved things -- `l` toggles something, and re-pressing it does
not land on the same place.

### What this section actually establishes

* **The control list was incomplete.** `circle` and `triangle` move at least one static screen; the
  six-control scans in section 50 could never have found that. This is a genuine coverage error and it
  is corrected.
* **A UI transition is distinguishable from scene motion** by checking staticness *after* the press: the
  same guard that catches animating scenes also separates "the screen changed and settled" from "the
  screen is just moving".
* **Reachability remains the binding constraint**, and it is subtle: `l` reaches a static screen
  repeatably for the *staticness* test, but not a screen that responds to a given control.

### The next test, now well-specified

The scan shows the right approach: **on a verified static screen, scan all twelve controls, and when one
both moves the display and leaves it static, immediately run the node probe with that control** -- all in
one process, with no `l` re-press in between. That is a single chained run, and it has not yet been done
in exactly that form.

### Method note

> **When a search fails everywhere, check the search's COVERAGE before concluding the target is absent.**
> Seven screens x six controls was read as "no screen is interactive", but the control list omitted the
> face buttons -- and two of them work. A negative from an incomplete input set is not a negative about
> the game.

> **Distinguish "changed and settled" from "still changing".** Requiring the screen to be static *after*
> a press separates a UI transition from ordinary motion, using the same measurement as the animation
> guard. One extra capture per press buys that distinction.



---

## 54. SCROLL vs TRANSITION: a sharper criterion, and it finds no scroll control

Section 53 credited `circle` and `triangle` as "MOVED and SETTLED", then the chained follow-up voided
because they moved the display **once** and not again. That exposed a distinction worth formalising,
because it separates two very different buttons:

| kind | behaviour |
|---|---|
| **SCROLL** control | moves the selection WITHIN a screen -- so it moves the display on **every** press |
| **TRANSITION** control | one press navigates to a *different* screen; further presses there do nothing |
| **inert** control | no effect |

A selection cursor needs a **scroll** control. The test is therefore direct: press a control N times and
require the display to move on **all N**.

### The test and its result

`psp-find-scroll-control.py --reps 5`, after reaching a screen verified static (no-press diff `0`):

```
   down      moved 0/5, settled 0/5   inert
   up        moved 0/5, settled 0/5   inert
   left      moved 0/5, settled 0/5   inert
   right     moved 0/5, settled 0/5   inert
   l         moved 0/5, settled 0/5   inert
   r         moved 3/5, settled 0/5   partial
   circle    moved 4/5, settled 0/5   partial
   cross     moved 4/5, settled 0/5   partial
   triangle  moved 4/5, settled 0/5   partial
   square    moved 4/5, settled 0/5   partial
   start     moved 3/5, settled 1/5   partial
   select    moved 0/5, settled 0/5   inert

No control moved the display on every press: no scroll control on this screen.
```

**No scroll control exists on this screen.** The D-pad and both shoulders are entirely **inert**; the
face buttons produce continuing motion which never settles (`settled 0/5` throughout), i.e. one press
leaves the static screen and the rest occur on an **animating scene**.

### What this settles

* **The earlier "MOVED and SETTLED" reading was a one-shot artefact.** A single press measured on either
  side of a transition looks like a settled UI change; repeating the press exposes it as a transition
  that leaves the screen. **A transition test needs repetition; a single press cannot tell a scroll from
  a transition.**
* **No screen reachable by blind input has a scroll control.** Combined with section 50 (seven screens,
  six controls) and this run (one verified-static screen, twelve controls, five repetitions each), the
  reachable set offers no scrollable list. The two screens where `down` *was* seen to scroll (the pause
  menu, verified twice by OCR) remain unreachable blind.
* **The D-pad being inert on these screens is itself informative**: it means the screens reached are not
  list menus at all -- list menus respond to the D-pad.

### Status of the cursor hunt

Every structural question except one is answered or verified-static, and the instrumentation is sound
(guards fired correctly in sections 47-49, 52, 54). The single open question -- **do the manager's node
fields track the highlight?** -- requires a screen with a scroll control, and the measured conclusion is
that blind input cannot reach one. That is a precise, evidenced stopping point for the automated phase.

### Method note

> **Repeat a press to classify the control.** One press cannot distinguish a scroll from a transition;
> five presses can, because a scroll moves every time and a transition moves once. The same reasoning as
> "a pattern needs more samples than it has states" (Rule 126) applied to *input* rather than to memory.

> **An inert control list is a clue about the screen, not just a dead end.** The D-pad being inert across
> every reachable screen says those screens are not list menus -- which is why no selection index was
> ever going to be exercised there.



---

## 55. Codex corrects my reading, and the render-node chain is validated against live RAM

Section 54's next move was to hand a bounded code question to a second agent: in `FUN_0025595c` I had read

```c
iVar2 = (uint)*pbVar5 - (int)*(short *)(param_2 + 0x18);
if (iVar2 < *(int *)(param_2 + 0x1c)) {
    iVar2 = *(int *)(*(int *)(param_2 + 0xc) + 0xc) + iVar2 * 0x1c;
}
```

as a **visible-window / scroll-offset** computation (`+0x18` base, `+0x1c` count, `+0xc` table). Codex
returned a correction and a working chain.

### The correction

The actual decompile in `render-writer-report.txt` reads:

```c
if ((short)(ushort)*pbVar5 < *(short *)(param_2 + 0x18)) {
    iVar2 = 0;
} else {
    iVar2 = (uint)*pbVar5 - (int)*(short *)(param_2 + 0x18);
    if (iVar2 < *(int *)(*(int *)(param_2 + 0xc) + 0x2c)) {
        iVar2 = *(int *)(*(int *)(param_2 + 0xc) + 0xc) + iVar2 * 0x1c;
    } else { iVar2 = 0; }
}
```

**Two errors in my reading:**

* **`param_2 + 0x1c` is not the bound.** The bound is `(*(param_2 + 0xc)) + 0x2c` -- one more
  dereference. `+0x1c` is a **callback pointer**, invoked by `FUN_00255170` as
  `(*(code **)(N + 0x1c))(read32(N + 0x20), ...)`.
* There is also a **guard** for `*pbVar5 < bias` that returns 0, which I had folded into the arithmetic.

So the "contiguous `+0x18`/`+0x1c`/`+0xc` window" I proposed was wrong: the count is not adjacent, it is
inside the descriptor the `+0xc` pointer leads to.

### The chain, validated against live RAM

Codex gave a recipe; every step dereferences cleanly on the running game:

```
M = read32(0x08B9B770)        = 0x08C08EB0
N = read32(M + 0x28)          = 0x08C096E0     (first-list head, rendered by FUN_00248dec)
  N + 0x18  bias   = 25
  N + 0x1c  (cb)   = 0x00000000                <- confirms CALLBACK, not count
  N + 0x0c  R      = 0x08C1475C
    R + 0x0c table = 0x09DFAF80
    R + 0x2c count = 11                        <- the REAL count
M + 0x34 (second-list head) = 0           (empty on this screen)
```

Independent corroboration: `FUN_0024aee0` sets `local_50 = N[3]` (i.e. `R`), compares a child's signed
short index against `local_50[0xb]` (`R + 0x2c`), and addresses `read32(R + 0x0c) + index * 0x1c` -- the
same table/limit pattern, from a different function.

Also useful: **`param_2` is the current outer render/menu node**, taken from one of the manager's two
linked lists, so the draw path's object identity is now pinned down:

```
param_2 in FUN_0025595c  ==  read32(read32(0x08B9B770) + 0x28)
```

### What Codex explicitly did NOT claim

It refused to call `N + 0x18` a scroll position or `R + 0x2c` a visible-row count, on four stated
grounds: the alleged count was a callback; the real count lives in the descriptor; the indexed value
`*pbVar5` is a byte in render/animation data rather than a demonstrated row ordinal; and **no supplied
body shows pad input updating any field in this chain as the selection moves**. Its conclusion: *"there
is no justified current-selection RAM recipe in the supplied artifacts"* -- and that a watchpoint on
`N + 0x18` could test the weaker scroll-bias candidate, but treating it as the selection without such a
trace would be speculation.

That is the right answer, and it is more useful than a guess: it narrows to **one concrete candidate
field (`N + 0x18`, bias = 25)** with a stated test.

### Method note

> **A second agent can catch a misreading of your own artifact.** The scroll-window interpretation came
> from my own summary of a decompile I had read; the agent re-read the file and showed the count was one
> dereference further in and that the adjacent field was a callback. Handing over the *artifacts plus the
> claim* is what made the correction possible -- the task included the claim being tested.

> **Ask for a refusal as a valid outcome.** The task said a clear "not resolvable from these artifacts,
> and here is what is missing" would be a useful answer. It was: the missing piece is the menu-side input
> consumer, or a before/after watchpoint trace on a moving menu.



---

## 56. A memory-level no-press control — and its first clean negative, on any screen

Section 54 ended stuck: every press test needed a **static** screen, because delivery was judged by a
pixel diff, and no reachable screen was both static and responsive. Section 56 removes that dependency.

### The idea

Delivery does not have to be judged by pixels. If a memory field changes when I press and does **not**
change when I don't, that is a genuine press response **at the memory level** -- the same logic as the
memory no-press control (Rule 120), which is indifferent to what the display is doing. So the test works
on **any** screen, animating or not.

### The instrument

`psp-watch-chain.py` samples the full chain Codex validated in section 55 -- 90 fields covering the
manager header, the render node `N`, its bias/callback fields, the descriptor `R`, and the first 24
entries of the `0x1c`-stride table -- across two phases:

1. **NO-PRESS phase** (8 samples): which fields vary on their own (churn);
2. **PRESS phase** (8 presses): which fields vary with input;
3. report fields that change **only** in the press phase.

### First result: a clean negative

```
=== analysis ===
  fields that vary with NO press (churn):        0
  fields that vary during presses:               0

=== PRESS-ONLY fields ===
   NONE -- nothing in the chain responds to 'down' uniquely.

=== the specific candidate Codex flagged ===
   N+0x18 bias   no-press: [25, 25, 25, 25, 25, 25, 25, 25]
   N+0x18 bias   pressed : [25, 25, 25, 25, 25, 25, 25, 25]
   -> constant under presses; not the selection on this screen.

=== liveness AFTER ===
  after ticks delta 625411018 -> EXECUTING
```

**This is the strongest negative the investigation has produced**, for three reasons:

* **zero churn** -- all 90 fields were constant across 8 no-press samples, so there is no noise to
  confuse a response with;
* **liveness asserted on both ends** (ticks advancing before and after);
* **the control does not depend on the display**, so it cannot be voided by an animating screen -- the
  failure mode that voided sections 47, 48, 49, 53 and 54.

So on this screen the entire render-node chain -- including Codex's one concrete candidate `N + 0x18`
(bias = 25) -- is **static under `down`**. That is consistent with section 54's finding that the D-pad is
inert on the reachable screens, and it means the candidate is not exercised here rather than disproven
in general.

### What this changes

* The **screen blocker is gone** for memory-level questions. Previously a candidate field could only be
  tested on a screen that was both static and responsive; now it can be tested anywhere, because churn is
  measured rather than assumed away.
* The remaining need is narrower than "find a scrollable screen": it is **any screen where a control
  produces a memory change**, which is a much easier thing to find than a pixel change on a static
  display.

### Method note

> **Choose the observation channel that does not depend on the fragile thing.** Pixel diffs need a static
> screen; memory needs only a no-press control. Every earlier void came from judging delivery through the
> display. Moving the judgement into memory removes an entire class of refusal -- and it is the same
> insight as Rule 120 (a field that moves with a press must not move without one), just applied to
> delivery rather than to the candidate.

> **Zero churn is a gift.** All 90 fields being constant without input means any change observed later is
> attributable; with churn present, a single sample proves nothing. Worth noting as a property to check
> *first*, because it determines how much sampling the rest of the test needs.



---

## 57. The node list does not hold the selection — 220 fields, zero churn, memory-level delivery

Sections 39, 47 and 53 each reported "no node field changed", and every one of those runs was either
void, taken on an unverified screen, or judged by pixel delivery. This section re-runs the test with the
instrument that section 56 established: a **memory-level no-press control**, which is valid on any screen
because it never depends on the display.

### The run

`psp-watch-nodes-wide.py --press down --samples 10`: samples **every word of every node** in the
manager's list (220 fields: 16 words plus 8 individual bytes per node, across the whole walk), over a
10-sample no-press phase and a 10-press phase.

```
=== liveness BEFORE ===  ticks delta ...  -> EXECUTING

=== NO-PRESS phase ===   220 fields each of 10 samples
=== PRESS phase ===      220 fields each of 10 presses

=== analysis ===
  fields sampled:                    220
  fields that VARY WITH NO PRESS:    0   (churn)
  fields that vary during presses:   0

=== PRESS-ONLY fields ===
   NONE -- nothing in the node list responds to 'down' uniquely.

=== liveness AFTER ===   ticks delta 625,894,051  -> EXECUTING

VERDICT: TRUSTWORTHY NEGATIVE -- zero churn and zero press response across the whole
         node list; the selection is not stored in these nodes.
```

### Why this negative is load-bearing

* **Zero churn** across 220 fields in 10 samples: there is no noise for a response to hide in, so a
  change would have been unmistakable.
* **Liveness asserted before and after.**
* **Delivery judged in memory**, so an animating screen cannot void it -- the failure mode that voided
  sections 47, 48, 49, 53 and 54.
* **Full coverage of the candidate**: every word and flag byte of every node, not a chosen subset.

### The caveat, stated plainly

`down` was already known to be **inert on the reachable screens** (section 54: the D-pad scored `0/5`
everywhere). So this run shows that the node list does not respond to `down` **on this screen**; it does
not exercise the node fields with a control that *does* move the selection.

The honest formulation: **the node list has now been tested on the strongest instrument available, and it
does not respond to the only control that is available to press.** Combined with:

* the manager header being static across 12 verified presses (section 45),
* the render array being *derived* state (section 41),
* the `+0x00` target struct being static (section 46),
* the full-RAM scans finding no wrapping word or byte (sections 38, 40),

the conclusion is that **nothing in the manager's object graph responds to the reachable input**. The
selection is either reached only through a control that blind input cannot exercise, or it lives outside
this object graph entirely.

### What would settle it

One thing, and it is not more scanning: **a screen where a control demonstrably moves the selection**,
plus a watchpoint or a memory diff on that screen. Codex's answer in section 55 named the same missing
piece -- *"the menu-side input consumer/state-update routine (or a verified moving-menu watchpoint
trace) showing which field changes when Up/Down changes the highlighted logical item."*

### Method note

> **A strong instrument converts a repeated negative into a settled one.** "No node field changed" had
> been reported three times (sections 39, 47, 53) and each time was dismissible -- void, unverified, or
> delivered through the display. The same finding from an instrument with zero churn and asserted
> liveness is a result. When a negative keeps recurring but keeps being untrustworthy, **upgrade the
> instrument rather than re-running the test.**

> **State the residual scope.** The negative is "does not respond to `down` on the reachable screens",
> not "is never the selection". Saying which is meant keeps the result usable: it tells the next attempt
> exactly what it needs (a reachable screen with a live control), instead of implying the question is
> closed everywhere.



---

## 58. The pad object is located — and the press test has a TIMING blind spot

Codex named the missing piece: *"the menu-side input consumer/state-update routine showing which field
changes when Up/Down changes the highlighted logical item."* To find a consumer you need the address it
consumes, so this section targeted the **pad object's button state**.

### The object is addressable and now resolved

```
pad slot  0x08B965B0  (= 0x08804000 + 0x003925b0, the DAT_003925b0 static)
pad obj   0x09EDA3A0
known flags word: pad + 0x264 = 0x09EDA604
```

`DAT_003925b0` holds `&DAT_0132fb40` -- a pointer, written at run time -- and dereferencing it gives the
pad object. The chain works, so the input state is addressable from a static.

### The scan, and the 95 responding fields

`psp-find-press-field.py --button cross --span 0x400 --samples 8`, with a memory-level no-press control
(valid on any screen):

```
  words sampled:              256
  vary with NO press (churn): 0        <- clean baseline
  vary during presses:        95
```

So **95 words of the pad object respond to a press**, against **zero churn**. Representative shapes:

```
   pad + 0x0F0   0x09EDA490 -> [0, 0, 0, 1106247680, 1106247680, ...]    (0x41F00000 = 30.0f)
   pad + 0x104   0x09EDA4A4 -> [3, 3, 0, 0, 0, 0, 0, 0]
   pad + 0x154   0x09EDA4F4 -> [3, 3, 0, 0, 0, 0, 0, 0]
   pad + 0x290   0x09EDA630 -> [0, 0, 0, 0, 0, 1111121292, 1111121292, ...]
```

### The flaw this exposes

**Every one of those series is latch-shaped, not pulse-shaped**: values hold steady, then change once
(`[3, 3, 0, 0, ...]`, `[0, 0, 0, V, V, V, ...]`). None shows the per-press alternation a live button bit
would produce.

The cause is a **timing blind spot in the method**: each press is sent with `frames=20` (a hold) and the
sample is taken after `sleep(0.6)` -- i.e. **after the button has been released**. So a *transient* field
(the held-button word) reads as unchanged, while only *latched* state (counters, flags set once, screen
transitions) survives to be observed.

**The clearest evidence is the known flags word itself.** `FUN_000f790c` sets `pad + 0x264` from the
polled buttons, so `pad + 0x264` **must** change while `cross` is held -- and it does **not** appear in
the 95 responding fields. That absence is the timing artefact, not a negative: I never sampled while the
button was down.

So the correct reading of this run:

* **valid**: the pad object is at `0x09EDA3A0`; there is zero churn in it; and 95 fields carry
  **latched** press-related state;
* **not valid**: "the pad's button word does not respond" -- the method cannot see transient state.

### The correction the next run needs

Sample **while the button is held**, not after. Concretely: send the press, sample immediately with **no**
settle delay, and repeat with a long hold (`frames=90`) so the button is down for the whole sample
window. Then a pulse-shaped field -- the live button word -- becomes visible, and that is the address an
input consumer must read.

### Method note

> **A sampling delay is part of the definition of what you can see.** Sampling after a press completes
> measures latched state; it is structurally blind to transient state, and a held button is transient by
> nature. The tell was that a field *known from the decompile* must change and did not -- so **when a
> known-positive fails to appear, suspect the sampling window before doubting the field.**

> **Use a decompile fact as a positive control.** `pad + 0x264` being written by the input poll gave a
> field whose behaviour is independently known. Its silence located the flaw immediately; without it, 95
> latch-shaped responses would have looked like success.



---

## 59. MAJOR CORRECTION: debugger input injection is INTERMITTENT — every "D-pad inert" result is suspect

While hunting the live button word, the pad object's `+0x00` was found to be the **`sceCtrl` button
word**, validated against documented PSP bit assignments:

```
button    expect   measured   observed during hold
select    0x0001   0x0000     no
start     0x0008   0x0008     MATCH   [0, 0, 0, 8, 8, 8]
up        0x0010   0x0010     MATCH   [0, 16, 16, 16, 16, 16]
right     0x0020   0x0020     MATCH   [0, 32, 32, 32, 0, 0]
down      0x0040   0x0000     no
left      0x0080   0x0080     MATCH   [0, 128, 128, 128, 128, 128]
l         0x0100   0x0000     no
r         0x0200   0x0000     no
triangle  0x1000   0x1000     MATCH   [0, 0, 4096, 4096, 4096, 4096]
circle    0x2000   0x2000     MATCH   [0, 8192, 0, 0, 0, 0]
cross     0x4000   0x4000     MATCH   [0, 16384, 16384, 16384, 16384, 16384]
square    0x8000   0x8000     MATCH   [0, 0, 32768, 32768, 32768, 32768]
```

Eight of twelve matched their documented bit **exactly**, and the pattern of the four that did not
(`select`, `down`, `l`, `r` reading `0x0000`) looked like a clean statement about which buttons work.

### It is not. The delivery is intermittent.

A dedicated reliability run -- each D-pad button pressed **3 times**, holding 90 frames, sampling the pad
word 7 times during the hold and 3 times after, with a 2 s settle between attempts:

```
idle word (no press): 0x0000

  UP     delivered in 0/3 attempts   (during: all 0x0000)
  RIGHT  delivered in 0/3 attempts   (during: all 0x0000)
  DOWN   delivered in 0/3 attempts   (during: all 0x0000)
  LEFT   delivered in 0/3 attempts   (during: all 0x0000)

   all four: NEVER DELIVERED across 12 attempts
```

But the **same buttons delivered in the previous run**: `up` read `0x0010` and `left` read `0x0080`
minutes earlier. And `down` read `0x0000` in one test and `0x0040` -- the correct down bit -- in another.

**So the same button, same hold length, same code path, reads delivered once and undelivered another
time.** That is a **race in the instrument**, not a property of the game or of the button.

### What this invalidates

Every conclusion of the form *"the D-pad is inert on the reachable screens"*:

* section 50 -- "7 screens, 6 controls, zero movement" (D-pad among them),
* section 54 -- "the D-pad scored 0/5 everywhere",
* section 56 -- "`down` is inert" used to caveat the chain negative,
* section 57 -- the node-list negative caveated with "`down` was already known to be inert".

Those findings cannot distinguish **the game ignoring a delivered press** from **the instrument not
delivering it**. Both produce "no change". The correct status of all of them is **instrument-suspect**,
not negative.

### What survives

* **The pad object and its button word are solid**: `pad = 0x09EDA3A0` (via `DAT_003925b0`), and
  `pad + 0x00` is the live `sceCtrl` button word carrying documented PSP bits -- confirmed for 8 buttons
  across two runs.
* **The face buttons and D-pad can both be delivered** -- each has been observed delivered at least once.
* **The pause menu really does scroll** (verified twice by OCR) -- so the D-pad *can* reach the game.

### The fix, and it is a gate rather than a hope

Delivery must be **verified per press, at the pad word**, and undelivered presses discarded:

```
press(button)
observed = sample pad+0x00 during the hold
if observed does not contain the button's documented bit:
    DISCARD this press -- do not count it as "no movement"
```

That converts the whole class of results from *"nothing happened"* to *"nothing happened **and the input
arrived**"*, which is the only form that can support a negative. It is Rule 120's no-press control
applied to the **input** side rather than the candidate side.

### Method note

> **Verify the INPUT, not just the output.** Every guard built so far asserted that the *system* was
> alive (ticks) and that the *display* was measurable (static). None asserted that the **press reached
> the game**. A silent input drop is indistinguishable from an inert control, and it produced four
> sections of misleading negatives. Instrument the input path itself.

> **An intermittent fault masquerades as a reliable negative when it is tested once.** `down` reading
> `0x0000` in the first bit test looked like a finding about that button. Re-testing the same button 3
> times per run, in a run that itself repeated, was what exposed it as a race.



---

## 60. The input gate works -- and `down` IS deliverable, re-opening the D-pad findings

Section 59 built the input gate. This section runs it, and it changes the picture in two ways at once.

### Delivery is verified per press

```
=== button 'up'     (expect bit 0x0010); need 5 DELIVERED presses ===
  delivered 5/5  (tried 9)
=== button 'down'   (expect bit 0x0040); need 5 DELIVERED presses ===
  delivered 5/5  (tried 9)          <- but see the correction below
=== button 'cross'  (expect bit 0x4000); need 5 DELIVERED presses ===
  delivered 5/5  (tried 11)
=== button 'circle' (expect bit 0x2000); need 5 DELIVERED presses ===
  delivered 5/5  (tried 21)
```

Each button required retries -- between 4 and 16 extra attempts to obtain 5 delivered presses -- which
**directly confirms section 59**: delivery drops frequently. The gate absorbed it instead of recording it
as "no movement".

### Result: `render count` responds to the D-pad

```
   up        fields 576 | churn 4 | PRESS-ONLY 1
      M+0x18_render      base 0            [0, 0, 0, 0, 3]
   down      fields 576 | churn 4 | PRESS-ONLY 1
      M+0x18_render      base 0            [0, 0, 0, 0, 5]
   cross     fields 576 | churn 4 | PRESS-ONLY 0
   circle    fields 576 | churn 4 | PRESS-ONLY 0
```

**`M + 0x18` (the render block count) is the one field that responds to DELIVERED presses of the D-pad**,
and it responds to `up` and `down` but not to `cross` or `circle`. That is exactly the signature expected
of a **menu that draws more or fewer rows as the selection moves** -- and it is the first field in this
whole investigation to correlate with a *verified* menu-direction press.

The values are small and grow (`0,0,0,0,3` / `0,0,0,0,5`) rather than stepping 1-per-press, so `+0x18` is
a **rendered-row count**, not the index itself: it is a *derived* quantity that depends on the selection,
one step removed. That is consistent with section 41's finding that the render array is derived state --
but it means the D-pad **does** drive a visible change in the manager on this screen, contradicting the
"inert D-pad" reading of sections 50, 54, 56 and 57.

### Correction to the section 59 narrative

Section 59 recorded the reliability run as showing the D-pad **never delivered (0/12)**. The gated probe
now obtains delivered `up` and `down` presses at **5/5 each** on the same emulator. So the correct
statement is:

> **Delivery is intermittent** -- sometimes every attempt in a run drops (as in the 0/12 run), sometimes
> most succeed. It is not that specific buttons never work, and it is not that a particular button is
> permanently dropped. Both readings were single-run artefacts.

This strengthens rather than weakens section 59's conclusion: an intermittent fault looks like a reliable
negative when sampled once, and the 0/12 run was itself one of those samples.

### What this means for the cursor hunt

* `M + 0x18` is a **derived** row count driven by the D-pad -- useful as a *detector* that a menu is
  being navigated, but not the selection index itself.
* The node sweep should now be re-run **with the gate**, because every previous node sweep either counted
  undelivered presses or used `down` without verifying it arrived. Section 57's "220 fields, zero press
  response" is now instrument-suspect for the same reason.
* The target is now clearer: find the field that `M + 0x18` is **derived from**. Since `+0x18` counts
  rendered rows and the render array base is `+0x14`, the index is plausibly the thing the writer
  (`FUN_0025595c`, section 42) iterates over -- the list index feeding the render loop.

### Method note

> **A gate converts "sometimes" into data.** Without it, four buttons each scoring 0/1 delivered would
> have read as four inert controls. With it, the retry counts (9, 9, 11, 21 tries for 5 deliveries)
> become a measurement of the failure rate, and every field result is conditional on the input having
> arrived.

> **Correlate with a verified direction press.** `M+0x18` moving for `up`/`down` but not `cross`/`circle`
> is the first result in this investigation tied to a confirmed menu-direction input. The asymmetry
> (direction keys vs face buttons) is itself evidence that the field is menu-navigation state.



---

## 61. The SELECTION recipe from the render loop -- extracted in code, and it resolves in RAM

Section 60 pointed at the render writer's **caller** as the place the index must come from. This section
decompiles that caller and extracts an explicit selection recipe from it.

### The caller: `FUN_0024aee0` (the 4,776-byte render loop)

`DisRenderCallers.java` (2 callers, `FUN_0024aee0` and `FUN_0025595c` itself) produced a 927-line report.
The render loop walks the manager's list and, for every node, computes a value passed to a draw call:

```c
local_50 = (int *)param_2[3];
for (puVar16 = (undefined4 *)*param_2; puVar16 != (undefined4 *)0x0;
     puVar16 = (undefined4 *)puVar16[0xf]) {                 // walk list; next at +0x3C
  if ((*(ushort *)(puVar16 + 8) & 0x8000) != 0) {            // active/visible flag
    if ((int)*(short *)(puVar16 + 3) < local_50[0xb]) {
      iVar13 = local_50[3] + *(short *)(puVar16 + 3) * 0x1c;  // index * 0x1c stride
    } else {
      iVar13 = 0;
    }
    iVar13 = FUN_0025595c(*puVar16, param_2, iVar13, puVar16[0xb], puVar16);
    *(int *)(param_1 + 0x1c) = *(int *)(param_1 + 0x1c) + iVar13;
  }
}
...
iVar13 = -1;
if ((undefined4 *)param_2[4] != (undefined4 *)0x0) {
  iVar13 = (*(short *)(param_2[4] + 4) + -1) * 0x10000 >> 0x10;   // <-- SELECTION, 1-based
}
FUN_002478e0(*(undefined4 *)(param_1 + 4), *(undefined4 *)param_2[4], iVar13);
```

Two things are explicit here:

1. **The node layout is confirmed a third time**: `puVar16 + 3` (i.e. `+0x0C`) is a **short index**, used
   with the **`0x1c` stride** and bounded by a count at `local_50[0xb]` -- matching `FUN_0025468c`
   (section 39) exactly.
2. **The selection recipe is:** `param_2[4]` (= `node + 0x10`) is a **pointer**; the value at
   `that_pointer + 4` read as a **signed short**, minus 1, is passed as the third argument to a draw
   call. A `- 1` on a stored integer handed to the renderer is the signature of a **1-based selection
   index**.

`param_2` is a node of the manager's list (`FUN_0024aee0` ends with `param_2 = param_2[9]`, i.e. it
advances by `+0x24` -- the node's `next` link, confirmed in sections 39 and 55).

### The recipe in RAM: it resolves, structurally

`psp-cursor-recipe.py` applies `q = read32(N + 0x10)`, `sel = read_s16(q + 4)` to every node, with the
**input gate** verifying each press at the pad button word:

```
manager 0x08C08EB0  nodes 33  render 0
   n0   q=0x08C160AC  SEL=10    idx=20936
   n1   q=0x08C16124  SEL=2     idx=16528
   n2   q=0x08C16110  SEL=2     idx=16412
   n3   q=0x08C160FC  SEL=2     idx=16296
   n4   q=0x08C160AC  SEL=10    idx=20588
   n5   q=0x08C16084  SEL=2     idx=15252
   n6   q=0x08C16070  SEL=3     idx=15136
   n7   q=0x08C15F30  SEL=2     idx=20472
```

**Every node's `q` is a valid RAM pointer**, all landing in a contiguous block
(`0x08C15F30`-`0x08C16214`), and `q + 4` reads a small ordinal. So the recipe is correct: `node + 0x10`
really is a pointer, and the pointer really leads to a struct whose `+4` is the small index the draw
call receives. That is an **independent confirmation from RAM of the decompiled expression**, and it
identifies a concrete per-node selection field.

### But SEL did not move under delivered presses

```
=== 'up'   (bit 0x0010): delivered 6/6 (tried 16) ===
=== 'down' (bit 0x0040): delivered 6/6 (tried 24) ===

   --- up ---     n0 [10,...]  n1 [2,...]  n4 [10,...]  n6 [3,...]   (all constant)
   --- down ---   n0 [10,...]  n1 [2,...]  n4 [10,...]  n6 [3,...]   (all constant)
   MOVES lines: 0
   render count: [0, 0, 0, 0, 0, 0]
```

**The readings are valid** -- 6/6 delivered both times, liveness asserted, and the retry counts (16 and
24 tries for 6 deliveries) again show the intermittence the gate is absorbing.

`SEL` values look like a **per-item stored ordinal** (a definition index), not the highlight: several
distinct nodes share `SEL=10`, `SEL=2` and even the same `q` pointer (`n0`, `n4`, `n12`, `n13`, `n16` all
`q=0x08C160AC`), which is consistent with shared item definitions rather than one moving cursor.

And critically: **`render count` was 0 on this screen.** Section 60 established that `M + 0x18` responds
to delivered D-pad presses -- but only on a screen that *draws rows*. Here nothing is being drawn, so the
render loop that consumes this recipe is not running, and a cursor would not be exercised.

### Status

* **Confirmed in code**: `node + 0x10` is a pointer; `(s16 at ptr + 4) - 1` is passed to a draw call as
  a 1-based index; the node index/stride (`+0x0C`, `0x1c`) is confirmed a third time.
* **Confirmed in RAM**: the pointer resolves and the field holds small ordinals -- the recipe is real.
* **Not established**: that `SEL` is the *highlight* rather than a per-item stored index (the repetition
  of values argues against), and whether it moves on a screen that actually draws -- this screen had
  `render count = 0`.

### Method note

> **Follow the writer to its caller, then read the caller's arguments.** The selection was not found by
> scanning; it was found by asking what the render loop passes to its draw call. `FUN_0025595c` said
> *where rows are written*; `FUN_0024aee0` says *which index each row is drawn with* -- and the `- 1`
> on a stored short is what identifies the second thing as a selection rather than a count.

> **A recipe can be valid and still not fire.** The recipe resolves cleanly in RAM and is confirmed by
> the decompile, yet it is static because the screen draws nothing (`render count = 0`). Validating the
> *structure* is separable from observing the *behaviour*, and saying which one was achieved keeps the
> result usable: the structure is settled, the behaviour needs a drawing menu.



---

## 62. Input gate fully operational -- nine controls verified, and this screen draws no rows

Section 61 extracted the selection recipe but could not exercise it because `render count` was 0. This
section sweeps **every control with the input gate**, using `M + 0x18` as a "this screen draws" detector.

### The run

```
  down     delivered 3/3 (tried 13)  RENDER max 0     SEL moved: none
  up       delivered 3/3 (tried  5)  RENDER max 0     SEL moved: none
  left     delivered 3/3 (tried  8)  RENDER max 0     SEL moved: none
  right    delivered 3/3 (tried  5)  RENDER max 0     SEL moved: none
  circle   delivered 3/3 (tried  6)  RENDER max 0     SEL moved: none
  cross    delivered 3/3 (tried 10)  RENDER max 0     SEL moved: none
  triangle delivered 3/3 (tried  6)  RENDER max 0     SEL moved: none
  square   delivered 3/3 (tried 10)  RENDER max 0     SEL moved: none
  start    delivered 3/3 (tried 10)  RENDER max 0     SEL moved: none
  l        delivered 0/3 (tried 30)  RENDER max None  SEL moved: none
  r        delivered 0/3 (tried 30)  RENDER max None  SEL moved: none

=== liveness AFTER ===   ticks delta 899,998,938 -> EXECUTING
```

### What is now solid

**The input gate works exactly as designed.** Nine controls were each verified delivered **3/3** at the
pad button word, so their "no change" readings are real evidence. `l` and `r` were delivered **0/30** each
and the script reports them with **no verdict** rather than as inert controls -- which is precisely the
distinction sections 50-57 could not make. The retry counts (5-13 tries for 3 deliveries) again measure
the intermittence the gate absorbs.

### The result

**`RENDER` stayed 0 for all nine verified-delivered controls, and no `SEL` field moved.** So this screen
draws no rows at all, and the render loop that consumes the section-61 recipe is not running. The
consequence is narrow and clear:

> The selection recipe is **structurally confirmed** (code + RAM) but has still never been observed
> *firing*, because a screen that draws rows has not been reached with a live control.

This is now a **bounded, well-posed** gap. It is not "the cursor is unknown"; it is "the recipe is
identified, and it needs a screen where `M + 0x18 > 0`".

### Why this is a good place to stop for evidence

Three things are true simultaneously and they are independent:

1. **`M + 0x18` is a proven detector.** Section 60: it responded to delivered D-pad presses (`up`/`down`)
   but not to `cross`/`circle` -- so it marks a drawing menu, and it is one 4-byte read.
2. **Delivery can be verified per press.** Section 62: 9/9 controls gated, 2 correctly refused.
3. **The recipe is known.** Section 61: `q = read32(node+0x10)`, `SEL = read_s16(q+4)`, 1-based.

So the remaining work is a **single run on a drawing screen**: sweep with the gate, watch `M + 0x18`, and
when it is non-zero read `SEL` per node. Everything needed for that is in place.

### Method note

> **A refused control is a result.** `l` and `r` scoring 0/30 delivered produced *no* claim about those
> buttons -- which is the correct outcome. Earlier sections recorded exactly this situation as "the
> D-pad is inert on the reachable screens" and it was wrong. Reporting the refusal, and naming it as a
> refusal, is what prevents the error.

> **Use a proven detector to define the remaining gap.** `M + 0x18` converts "we need a menu" into a
> measurable predicate (`> 0`), which turns an open-ended search into a checkable condition. Naming the
> gap as a predicate is what makes it closable in one run.



---

## 63. OVERTURNS SECTION 54: with the gate, the D-pad DOES move this menu -- and 4 words survive a gated diff

Two runs this section, both with the input gate, and together they overturn a conclusion that had stood
since section 50.

### Run 1: the SCROLL test, gated

Section 54 concluded, without a gate:

> "No control moved the display on every press: no scroll control on this screen. Transitions moved once;
> the D-pad and shoulders were inert."

The same controls, **each press verified at the pad button word while held**:

```
control   delivered-press result        per-delivered-press diff
  down      3/4 delivered presses moved  [0, 245406, 245472, 245397]
  up        3/4 delivered presses moved  [245397, 245415, 245397, 0]
  left      3/4 delivered presses moved  [245415, 0, 8645, 6499]
  right     1/4 delivered presses moved  [0, 0, 4434, 0]
  circle    4/4 delivered presses moved  [163217, ...]
  cross     4/4 ...
  triangle  4/4 ...
  square    4/4 ...
  l         NO VERDICT (0 delivered)
  r         NO VERDICT (0 delivered)

=== verdict ===
  acted on this screen (delivered AND moved): ['down','up','left','right','circle','cross','triangle','square']
  genuinely inert (delivered, never moved):   ['start','select']
  NO VERDICT (could not be delivered):        ['l','r']
```

**`down` moved the display with a consistent ~245,400 px change on three of four delivered presses.** A
repeated, near-identical per-press delta is the signature of a **highlight moving between rows** -- not
scene motion, which is variable (compare `left`'s `8645 / 6499` and `right`'s `4434`).

So the three-way distinction section 54 could not make is now explicit:

| result | meaning |
|---|---|
| delivered + moved | the control acts on this screen |
| delivered + never moved | genuinely inert (**valid** negative) |
| not delivered | **no verdict** (instrument) |

**Section 54's "the D-pad and shoulders were inert" is overturned**: the D-pad acts here, and section 59's
intermittence explains why the ungated run saw nothing.

### Run 2: full-RAM diff around GATED presses, intersected

This is the first screen in the investigation with **both** a demonstrated per-press D-pad response **and**
a working input gate -- so the full-RAM scans (sections 38, 40) that produced negatives on unverified
delivery are worth redoing. Method: read all 24 MB, gate a press, read again, record changed words, then
**intersect across rounds and subtract a no-press control**.

```
  round 1: baseline read 8.8s   delivered; changed words: 1341
  round 2:                        delivered; changed words: 1309
  round 3:                        delivered; changed words: 1302
  round 4:                        delivered; changed words: 1278
  no-press control: changed words 1320

=== INTERSECTION across 4 delivered rounds: 913 words ===
=== after removing no-press churn: 4 words ===
  0x08BB4130  1 word   [-65536]        (0xFFFF0000)
  0x08BB42D8  1 word   [-65536]
  0x08BB4EE8  1 word   [-65536]
  0x08C0BD7C  1 word   [1073741824]    (0x40000000)
```

**From 9,000+ changed words across four runs, exactly 4 survive both the intersection and the churn
filter.** That is the most selective result in the entire investigation, and it rests on a screen where
the input is verified to land.

Two observations:

* **Three carry `0xFFFF0000`.** That value is `-65536`, i.e. `0xFFFF` in the high half -- consistent with a
  negative or sentinel marker, or with a fixed-point quantity changing sign, in three separate structures.
* **One carries `0x40000000`** = `1073741824` = **2.0f** -- a float exactly 2.0.

These are **candidates, not yet the cursor**: nothing here yet shows the 1-based small ordinal a menu
highlight would have. But they are the only four words in 24 MB that respond to a verified menu-direction
press on a screen where that press demonstrably moves the display, and each is a bounded, single-address
follow-up.

### Method note

> **An ungated negative is not a negative.** Section 54 recorded "the D-pad is inert" and it was the
> gate's absence, not the game's behaviour. The same test, gated, shows the D-pad acting with a
> consistent per-press delta. Any conclusion of the form *"this control does nothing"* requires the
> three-way table above, and only the middle row is a negative.

> **A consistent per-press delta distinguishes a highlight from animation.** `down` moved ~245,400 px on
> three successive verified presses; the scene-motion controls moved by irregular amounts (8,645 / 6,499 /
> 4,434). Reproducibility of the delta is itself evidence of the mechanism.

> **Redo scans when the instrument improves.** The intersection-and-churn method had been run before
> (sections 31, 40, 60) but never on a screen with verified delivery; re-running it here collapsed 9,000
> changed words to 4. The instrument, not the search space, was the limitation.



---

## 64. The top candidate FAILS the churn test -- and the gated diff's churn filter has a SAMPLING ALIAS

Section 63's gated full-RAM diff reduced 9,000+ changed words to four survivors. This section tests them
properly, and one of them produced the most cursor-like signal of the whole investigation -- then failed.

### Two survivors respond, and one is striking

```
   delivered 5/5 (tried 7)
   0x08BB4130  [4294901760, 4294967295, 4294901760, 4294901760, 4294967295]  <== RESPONDS
   0x08BB42D8  [4294901760, ...]  constant
   0x08BB4EE8  [4294901760, ...]  constant
   0x08C0BD7C  [1090519040, 1088421888, 1088421888, 1086324736, 1073741824]  <== RESPONDS
```

Decoded, `0x08C0BD7C` reads as **floats**: `1090519040` = `0x41000000` = **8.0**, `1088421888` = **7.0**,
`1086324736` = **6.0**, `1073741824` = **2.0**. A float holding small integers, decrementing as `down` is
pressed. `0x08BB4130` alternates between `0xFFFF0000` and `0xFFFFFFFF` (`-65536` / `-1` as ints).

A float that counts down in integers, on a screen where each `down` visibly moves the highlight, is the
most cursor-shaped signal found in sixty-four sections.

### It failed the churn test, and that is the result

`0x08C0BD7C`, sampled **16 times at 1 s intervals with NO input at all**:

```
  t= 0s float 10    t= 4s float 9    t= 8s float 9    t=12s float 9
  t= 1s float  7    t= 5s float 6    t= 9s float 6    t=13s float 2
  t= 2s float  4    t= 6s float 4    t=10s float 3    t=14s float 0
  t= 3s float  1    t= 7s float 1    t=11s float 1    t=15s float 9

distinct values: [0, 1, 2, 3, 4, 6, 7, 9, 10]
CHANGED WITHOUT INPUT: True
```

It cycles continuously **with no input**. It is a running counter (an animation/effect value), not a
cursor.

### Why the gated diff's churn filter missed it -- a real flaw

Section 63's method read all 24 MB, then read again, and subtracted any word that differed during a
**no-press window of the same duration**. That control *should* have caught a continuously running counter.

It did not, because **the full-RAM read takes ~8.8 s per pass**, so each control comparison spans roughly
**17 s**. `0x08C0BD7C` cycles with a period of about 5 s, so across a 17 s window it returns to a similar
value and can be scored "unchanged" by coincidence. **A long read window aliases against a fast cycle**,
and the churn filter silently passes periodic values through.

That is the same class of error as the coarse 8x8 grid hash (section 33) and the sampling delay of
section 58: **the measurement window interacts with the signal's period.** This time the window is long,
not short.

### What stands, and what does not

* **Overturned**: `0x08C0BD7C` as the cursor. It is a free-running counter, evidenced by its behaviour
  with no input at all.
* **Not established**: `0x08BB4130`, which alternates `0xFFFF0000` / `0xFFFFFFFF` under delivered presses.
  Two values only, and a no-press re-test has not been run on it specifically.
* **Section 63's headline correction still stands**: the D-pad **does** move this menu under verified
  delivery (three consecutive `down` presses of ~245,400 px each), overturning section 54. That result
  did not depend on the churn filter.
* **Section 63's "4 words survived" is weakened**: the filter that produced it is now known to pass
  periodic values. The intersection step remains valid (a churning word would rarely be in *every*
  round's changed set), but the churn subtraction is unreliable at this read duration.

### The fix

**Churn must be measured with a sampling rate matched to the signal, not inherited from the read
duration.** Concretely: after the full-RAM diff nominates candidates, re-test **each candidate
individually** with a fast poll (0.2-1 s, many samples, no input) before believing it. A single-address
read is ~1 ms, so the control is cheap; the 8.8 s full-RAM read is what created the alias.

### Method note

> **A churn filter inherits the sampling window of whatever produced it.** Taking the control at the same
> coarse cadence as the scan means a periodic signal can survive it. When the scan is slow (24 MB), the
> control must still be fast -- **measure churn per candidate, at a rate well above the candidate's
> expected period**, rather than once for the whole scan.

> **Check the top candidate by its behaviour with no input, before interpreting any press response.** The
> press series for `0x08C0BD7C` looked like a monotonically stepping integer (8,7,6,2) and was convincing;
> 16 seconds of no-input sampling showed it cycling `10,7,4,1,9,6,...` the whole time. The no-press
> control is what separated the two, and it cost 16 seconds.



---

## 65. ALL FOUR candidates rejected -- the fast churn poll shows every one is a free-running counter

Section 64 diagnosed the flaw: the churn control for the full-RAM diff was taken at the **same coarse
cadence as the scan** (~8.8 s per 24 MB pass, so ~17 s per comparison), which lets a signal with a period
shorter than the window alias back to a similar value and pass as "unchanged". The fix is to measure churn
**per candidate at a rate well above the candidate's expected period**.

`psp-fast-churn.py` does exactly that: 40 polls at 0.25 s (~10 s) per candidate with **no input**, then a
gated press phase. Result:

```
=== PHASE 1: NO-INPUT churn, 40 samples at 0.25s ===
  0x08BB4130  3 distinct value(s)   CHURNING -- reject
  0x08BB42D8  2 distinct value(s)   CHURNING -- reject
  0x08BB4EE8  3 distinct value(s)   CHURNING -- reject
  0x08C0BD7C  11 distinct value(s)  CHURNING -- reject
      series: 3, 0, 8, 5, 2, 10, 7, 4, 1, 9, 6, 3, 0, 8, 5, 2 ...

=== PHASE 2: gated press phase ===
  delivered 6/6 (tried 10)

=== VERDICT per candidate ===
  0x08BB4130  3 vals  yes  REJECT (churns with no input)
  0x08BB42D8  2 vals  no   REJECT (churns with no input)
  0x08BB4EE8  3 vals  yes  REJECT (churns with no input)
  0x08C0BD7C  11 vals yes  REJECT (churns with no input)
```

**Every one of section 63's four survivors churns with no input at all.** The `0x08C0BD7C` series is a
clean **period-11 counter**: `3, 0, 8, 5, 2, 10, 7, 4, 1, 9, 6` then repeating. That is a 10-value cycle
(skipping a step), i.e. an animation or effect counter, and it fully explains why its press series looked
like stepping integers -- the press series was sampling the cycle at unrelated phase offsets.

### What this says about the gated full-RAM diff

The method was:

1. read all 24 MB, gate a press, read again -> the changed-word set;
2. **intersect** across rounds;
3. **subtract** a no-press control taken at the same cadence.

Step 3 is now known to be broken for short-period signals. Step 2, however, is still doing real work: a
free-running counter of period 11 would have to be caught in the *changed* set of **every** round to
survive the intersection, and with 4 rounds at ~11 s spacing that is possible by aliasing -- which is
exactly what happened. So both steps were necessary but neither was sufficient: **the intersection does not
protect against periodicity, and the coarse control does not detect it.**

### Consequence

The correct conclusion is narrower than section 63's: **the gated full-RAM diff at ~17 s resolution does
not isolate the selection index.** Any candidate it nominates must be checked with a fast per-candidate
churn poll before being believed -- and when that check is applied, this screen yields **zero** surviving
candidates.

That is a negative about the *instrument* at 24 MB scale, not about the game: the scan is too slow
relative to the game's animation rates to separate selection state from rendering state by differencing.

### Where that leaves the hunt

Three things are established and they jointly point at the remaining route:

* **The D-pad works on this screen** (section 63: ~245,400 px per verified `down` press) -- so a
  *watchpoint* has a real target: whatever writes the highlighted row.
* **The full-RAM differencing route is exhausted at this scale** -- 24 MB per sample aliases against
  animation; this section is the evidence.
* **The code route produced the recipe** (section 61: `SEL = read_s16(read32(node+0x10) + 4)`) but the
  recipe did not move here.

So the remaining approaches are, in order of expected value:

1. **a write watchpoint on the render array or the node fields, while a verified D-pad press moves the
   highlight** -- PPSSPP supports watchpoints (section 43) and delivery can now be verified;
2. **narrow the diff to a bounded region** where the animation counters are not (the earlier 0.25 MB
   region diff was reliable in sections 31/33 precisely because it was fast);
3. **follow the draw call from the recipe** -- `FUN_002478e0(manager+4, node+0x10, index)` receives the
   candidate index; decompiling it may name the field it reads back.

### Method note

> **Fast controls beat thorough ones when the signal is periodic.** A 1 ms single-address read repeated 40
> times in 10 s detected a period-11 counter that a 17 s full-RAM comparison could not. **Match the control's
> sampling rate to the signal's period, not to the cost of the primary measurement.**

> **Intersection protects against noise, not against periodicity.** A periodic value can appear in every
> round's changed set by aliasing, so intersecting rounds does not filter it. Periodicity needs a *rate*
> check, not a repetition check.

> **Report an instrument limit as an instrument limit.** The 24 MB differencing route is exhausted *at this
> sampling resolution* -- stated that way it directs the next attempt (watchpoint or bounded region) instead
> of implying the selection is unfindable.



---

## 66. Region sweep at 1.1 s resolution -- 10 of 96 regions respond, and the churn filter is now sound

Section 65 established that the 24 MB scan cannot separate selection from animation because a ~17 s
comparison lets short-period counters alias. The fix it named is to make the measurement fast relative to
the signal. This section does that: **96 regions of 256 KB**, each read in **0.053 s**, so a
baseline/press/after pass spans about **1.1 s** -- comfortably below the ~2.5 s cycle of the counter that
defeated the earlier attempt.

### Pass 1: which regions respond at all

```
region size 0x40000  x 96 regions   one region read: 0.053s
=> a baseline+press+after pass per region spans roughly 1.1s

  region  14 0x08B80000  385 changed word(s)
  region  15 0x08BC0000    3
  region  16 0x08C00000    7
  region  18 0x08C80000   18
  region  47 0x093C0000   30
  region  81 0x09C40000  196
  region  82 0x09C80000  196
  region  87 0x09DC0000   46
  region  94 0x09F80000    2
  region  95 0x09FC0000    7

  regions with any change: 10 of 96
```

Only **10 of 96 regions** respond to a delivered `down` press at all. That is a strong selectivity gain
over the full-RAM approach, and it was achieved purely by making the measurement fast.

### Pass 2: intersect across 3 more gated rounds

```
  0x08B80000  sizes [385, 441, 399, 417] -> intersection 1
  0x08BC0000  sizes [3, 5, 3, 3]         -> intersection 3
  0x08C00000  sizes [7, 5, 5, 4]         -> intersection 4
  0x08C80000  sizes [18, 18, 18, 18]     -> intersection 18
  0x093C0000  sizes [30, 30, 30, 30]     -> intersection 30
  0x09C40000  sizes [196, 196, 196, 196] -> intersection 196
  0x09C80000  sizes [196, 52, 52, 52]    -> intersection 52
  0x09DC0000  sizes [46, 45, 45, 45]     -> intersection 44
  0x09F80000  sizes [2, 2, 2, 2]         -> intersection 2
  0x09FC0000  sizes [7, 16, 18, 16]      -> intersection 7
```

Note `0x08B80000`: 385 changed words in round 1 collapsing to **1** across four rounds -- exactly the
behaviour that separates a real per-press effect from churn. Conversely `0x093C0000` holds **30 identical**
words across all four rounds, which at 1.1 s resolution is more likely a genuinely press-driven set than
an aliased counter (the earlier period-11 counter could not hold *identical* values at this cadence).

### Pass 3: per-word fast churn -- and it now rejects almost everything

Every survivor was polled 40 times at 0.2 s with no input. Representative output:

```
  0x08BF8D68  churns (40 values) -- rejected
  0x08C0BD7C  churns (11 values) -- rejected      <- the section-63 candidate, caught again
  0x08C890C0  churns (40 values) -- rejected
  0x093D3780  churns (40 values) -- rejected
  0x09C437D4  churns (38 values) -- rejected
  0x09C43844  CLEAN (1 value: 0x7E808080)
  0x09C439C4  CLEAN (1 value: 0xB0808080)
  0x09C43A84  CLEAN (1 value: 0xCA808080)
  0x09C43AE8  CLEAN (1 value: 0x04040004)
```

**The churn filter is now doing its job**: at 0.2 s sampling it catches everything that moves on its own,
including `0x08C0BD7C` (the counter that previously slipped through). That is the direct payoff of section
64's fix.

Four words survive to the end, all in the `0x09C40000` region:

```
  0x09C43844   0x7E808080
  0x09C439C4   0xB0808080
  0x09C43A84   0xCA808080
  0x09C43AE8   0x04040004
```

### Reading the four survivors

The values are the signature of **glyph bitmaps, not menu state**: `0x80808080`, `0xB0808080` and
`0xC0808080` are all-`0x80`-ish patterns typical of alpha/mask bytes in a font cache, and they differ in
their high byte (`0x7E`, `0xB0`, `0xCA`, `0x04`). The region `0x09C40000` is the same area identified in
section 22 as the **UI text/font pool** (the UTF-16LE menu strings sit at `0x09D16A68`+, and the glyph
atlas precedes them).

So the most likely reading is that these are **glyph-cache bytes whose high byte is a dirty/serial marker**
that changes when a different string is drawn -- i.e. *derived from* the selection, not *equal to* it. That
is consistent with the earlier finding (section 41) that the render array is derived state, and with the
observation that the four values have nothing in common with a 1-based row index.

### What this section establishes

* **The instrument is fixed.** At 1.1 s per region and 0.2 s per churn sample, the alias problem is gone
  and the churn filter rejects the counter it previously passed (section 65's fix, verified).
* **10 of 96 regions respond** to a delivered D-pad press -- a 10:1 selectivity gain over full-RAM.
* **The survivors are glyph bytes**, i.e. derived drawing state in the font pool, not the selection index.
* **The menu's real state change is not in a small-integer field** in the region set that responds.

### The route this leaves

Three attempts at differencing (full-RAM, 0.25 MB region, 96 x 256 KB region) now agree: what changes on a
verified menu-direction press is **drawing data**, and the logical index is either written earlier in the
frame than any diff can catch, or it lives in a structure whose write is masked by re-derivation each
frame.

That is exactly what a **write watchpoint** resolves and differencing cannot: it reports *who wrote*, not
*what differs*. Sections 43 and 60 established that PPSSPP supports watchpoints and that delivery can be
verified, so the watchpoint on the render array -- with a *verified* press moving the highlight -- is now
the specific next step.

### Method note

> **Make the measurement faster than the signal and the artefacts disappear.** The same search that
> returned a period-11 counter at 17 s resolution returned a clean per-word answer at 1.1 s. Nothing about
> the game changed; the resolution did -- and the 10-of-96 selectivity came for free with it.

> **A filter is only trustworthy once it rejects something you know is bad.** The per-word churn poll
> re-caught `0x08C0BD7C` -- the counter that had previously passed. That is the concrete evidence the fix
> works, and it is worth running a known-bad input through any new filter before trusting its output.

> **Check whether the survivors are the kind of thing you are looking for.** Four words whose values are
> all-`0x80`-patterned, in the font region, differing in their high byte, are glyph-cache markers. Asking
> what *class* the survivors belong to is cheaper than following each one, and it redirected the hunt.



---

## 67. The write watchpoint FIRES -- and halts inside `FUN_0025595c`, the render-block writer

Section 66 concluded that differencing is exhausted and the next step is a **write watchpoint**: it reports
*who wrote*, not what differs. This section runs one, correctly, for the first time -- and it lands on the
routine already identified in section 42.

### Why the earlier watchpoint attempts failed, and how this one differs

Section 43's attempt found no hits for two reasons, both now fixed:

* the emulator had been left **frozen** by debugger churn, so nothing executed -- now guarded by asserting
  `ticks` advancing before the run;
* it watched a **remembered data address from a different screen** -- now the address is resolved from the
  live manager each run.

`psp-watch-write.py` resolves and watches, live:

```
manager 0x08C08EB0
  render base  = read32(M+0x14) = 0x09DEE3C0
  render count = M+0x18          = 0x08C08EC8   (the field section 60 proved responds to the D-pad)
  node head    = read32(M+0x28) = 0x08C094FC
```

### The first run: the CPU halted, and that masqueraded as "no hits"

```
=== driving VERIFIED presses ===
  delivered 0/6 (tried 40)
  breakpoint/CHK log events captured: 0
=== liveness AFTER ===   ticks delta 0 -> FROZEN
```

`delivered 0/6` with a **frozen CPU afterwards** is the diagnosis: the breakpoint **fired and halted the
CPU**, so subsequent presses could not register and no further log events arrived. The script's own
"No hits" branch also lists the correct causes, and this is the second one -- *the CPU was not executing* --
reached not by staleness but by the watchpoint doing its job. (The script did not resume after a hit; that
is a defect to fix.)

### Capturing the halt PC directly

Arming a write watchpoint on the render count `0x08C08EC8`, pressing `down`, and reading `cpu.status`:

```
  run 1: pc=0x08A5A0D0   (before pc=0x08A5A0D0)
  run 2: pc=0x08A5A0D8   (before pc=0x08A5A0D8)
  run 3: pc=0x08A59F28   (before pc=0x08A59F28)
  run 4: pc=0x08A59F30   (before pc=0x08A59F30)
  run 5: pc=0x08A59F58   (before pc=0x08A59F58)
```

**`before pc` equals the post-press `pc` in every run**, and the PCs advance monotonically in address
order. Both facts say the CPU is **halted and single-stepping in a debugger loop**, not sitting at a
varied breakpoint PC -- `stepping: True` throughout confirms it. So the halt is real and repeatable, and it
is caused by the watchpoint.

### The PCs land inside `FUN_0025595c`

Converting RAM to vaddr (`ram - 0x08804000`):

```
ram 0x08A59F28 -> vaddr 0x00255F28
ram 0x08A59F30 -> vaddr 0x00255F30
ram 0x08A59F58 -> vaddr 0x00255F58
ram 0x08A5A0D0 -> vaddr 0x002560D0
ram 0x08A5A0D8 -> vaddr 0x002560D8
```

**All five lie inside `FUN_0025595c`** -- the render-block writer, base `0x0025595c`, size 2,108 (ends at
`0x00256198`). Section 42 verified that this function writes the `0x50`-stride render blocks and increments
the count at `DAT_00397770 + 0x18`:

```c
puVar12 = (*(int *)(DAT_00397770 + 0x14) + *(int *)(DAT_00397770 + 0x18) * 0x50);
*(int *)(DAT_00397770 + 0x18) += 1;
*puVar12 = pbVar5;  puVar12[1] = psVar4;  ...
```

So the watchpoint does not merely fire -- it halts **inside the exact routine that writes the watched
field**. The PC window (`0x00255F28`-`0x002560D8`, ~430 bytes) is the code around those stores.

### What this establishes and what it does not

**Established:**

* **PPSSPP write watchpoints fire and halt the CPU**, on a live-resolved address, with delivery verified
  separately -- the capability section 43 could not demonstrate.
* **The writer of the render count is `FUN_0025595c`**, confirmed dynamically at run time rather than only
  from the decompile. That closes the loop between section 42 (static claim) and live behaviour.
* **The halt PC is reachable and repeatable**, so a mapping from PC to the precise store instruction is
  now available.

**Not established:**

* Which *instruction* within `FUN_0025595c` corresponds to each PC -- the `stepping` loop means the PC is
  where PPSSPP paused, not necessarily the faulting store. Mapping needs the disassembly at
  `0x00255F28`-`0x002560D8`.
* Anything about the selection index: this names the code that writes the **render count**, which section
  60 showed is a *derived* row count, not the index itself.

### The fix this section names

The watchpoint script must **resume after each hit** and **record the PC per hit**, rather than driving a
press loop that starves once the CPU halts. Concretely: arm, press, read `cpu.status.pc`, `cpu.resume`,
repeat -- which is exactly the loop used above and which produced five clean readings.

### Method note

> **A halt can look like a failure.** `delivered 0/6` plus a frozen CPU was the signature of success for a
> watchpoint -- the breakpoint stopped the world, so nothing else could happen. The distinguishing evidence
> was `before pc == after pc` with advancing addresses and `stepping: True`: a debugger stepping loop, not
> an idle CPU. **When an instrument halts the system, a null downstream result is expected, not alarming.**

> **Map the halt PC back to the address space you already have decompiled.** Converting RAM to vaddr and
> finding all five PCs inside `FUN_0025595c` -- a function already identified as the render writer --
> turned five opaque numbers into a dynamic confirmation of a static claim, without any new decompilation.



---

## 68. The halt PCs are decoded -- `sw a3,0x18(a2)` IS the store that writes the watched field

Section 67 captured five watchpoint halt PCs inside `FUN_0025595c` but could not say which instruction
wrote the watched field. Disassembling the window `0x00255E80`-`0x00256198` answers it exactly.

### The three halt PCs in the first cluster

```
00255f28   lw    a3,0x18(a2)              <== HALT PC
00255f2c   ...
00255f30   bnel  a3,zero,0x00255f40       <== HALT PC
00255f58   sw    a3,0x18(a2)              <== HALT PC  [STORE]
00255f64   sw    a1,0x0(s6)               [STORE]
00255f68   sw    a0,0x4(s6)               [STORE]
00255f6c   swc1  f22,0xc(s6)              [STORE]
00255f70   sw    s3,0x8(s6)               [STORE]
```

**`00255f58  sw a3,0x18(a2)`** is a store to **offset `0x18`** off a base register -- precisely the field
the watchpoint was armed on (`manager + 0x18`, RAM `0x08C08EC8`). The instruction immediately before the
first two halt PCs, `00255f28  lw a3,0x18(a2)`, is the matching **read** of the same field, and
`bnel a3,zero,0x00255f40` branches on it.

So the pattern in this function is exactly the section-42 decompile:

```c
*(int *)(DAT_00397770 + 0x18) = *(int *)(DAT_00397770 + 0x18) + 1;
```

* `lw a3,0x18(a2)` reads the count,
* `bnel` tests it,
* `sw a3,0x18(a2)` writes it back.

And the block immediately following -- `sw a1,0x0(s6)`, `sw a0,0x4(s6)`, `swc1 f22,0xc(s6)`, `sw s3,0x8(s6)`
-- writes the render block fields at offsets `0x0`, `0x4`, `0x8`, `0xc` off `s6`. That is **the `0x50`-stride
render block being populated**, matching the four measured pointers per block found in RAM in sections
40-41 (`+0x00`, `+0x04`, `+0x08` were the three that alternated).

### The second cluster: two more stores, both inside a block

```
002560d0   swc1  f12,0x10(s6)   <== HALT PC  [STORE]
002560d8   swc1  f12,0x24(s6)   <== HALT PC  [STORE]
002560e8   swc1  f12,0x40(s6)   [STORE]
```

These write floats at offsets `0x10`, `0x24`, `0x40` off `s6` -- and `0x50` is the block stride, so all
three fall within one block. `f12` is written repeatedly, which is the signature of **initialising a block's
float fields to a constant** (a cleared transform/matrix), not of storing a selection.

### What this settles

* **The watchpoint hit is explained precisely.** The armed address `manager + 0x18` is written by
  `sw a3,0x18(a2)` at `0x00255f58`, and the halt PCs are the neighbouring read/branch. That is an
  end-to-end trace: static decompile (section 42) -> dynamic watchpoint (section 67) -> the exact store
  instruction (this section).
* **The render count's writer is confirmed at instruction level.** Section 60 showed `M+0x18` responds to
  delivered D-pad presses; section 42 predicted the increment; this section shows the instruction doing it.
* **The block fields are written immediately after the count**, so the sequence in this routine is
  *increment the count, then fill the block* -- i.e. `+0x18` is the number of blocks written so far, which
  is why it is a **derived row count** rather than a selection.

### What it does not settle

The selection index is still not identified. This routine is confirmed to **consume and produce render
state**; the index it draws for must be supplied by its caller. Two halt PCs remain unexplained in that
sense -- `0x002560d0`/`0x002560d8` are float initialisations inside a block, and no halt PC in this window
corresponds to reading a selection field.

The next bounded step is therefore the same shape as section 61 but aimed one level out: take the
**arguments** `FUN_0025595c` receives (section 61 recorded `FUN_0025595c(*puVar16, param_2, iVar13,
puVar16[0xb], puVar16)` where `iVar13` is the table entry at `index * 0x1c`) and watch **which caller-side
field produced `iVar13`** -- i.e. put the watchpoint on the *source index* rather than on the count it
produces.

### Method note

> **A halt PC plus a disassembly window converts a dynamic hit into an instruction-level claim.** Section
> 67 could only say "the watchpoint halts inside `FUN_0025595c`". Listing ±200 bytes around the halt PCs
> turned that into `sw a3,0x18(a2)` -- and the matching `lw`/`bnel` beside it confirmed the
> read-test-write shape the decompile predicted.

> **Look for the sibling stores to identify the data structure.** The stores immediately after the count
> write go to `0x0`, `0x4`, `0x8`, `0xc` off another register -- the block fields. Their presence, at those
> offsets, is what identifies the routine as *filling a render block* rather than merely bumping a counter,
> and that is the fact which makes `+0x18` a count rather than an index.



---

## 69. Watching the index-source structures: hits land in the render loop and the node-definition lookup

Section 68's next step was to watch the structures that could hold the selection index, rather than the
derived count. This section does that, with the hit-handling defect from section 67 fixed (arm -> press ->
read PC -> resume -> re-arm).

### Targets resolved live

```
manager 0x08C08EB0  N 0x08C0944C  R 0x08C13DD8  table 0x09DFF09C  count 37
```

Three watchpoints, each pressed 60 times with the CPU resumed before every press:

| target | address | hits |
|---|---|---|
| `0x1c`-stride table | `0x09DFF09C` (size `0x400`) | **0** |
| node N fields | `0x08C0944C` (size `0x40`) | **0** |
| node N+1 fields | `0x08C09344` (size `0x40`) | **6** |

Two clean negatives and one positive. The negatves are meaningful because the CPU was **explicitly
resumed before each press** and liveness was asserted at the start -- so "no hits" means the field was not
written during the press window, not that the world was stopped.

### The six hits, mapped to functions

```
0x08A4D398  vaddr 0x00249398   -> FUN_0024932c   (node-list manager: counters, free pool)
0x08A58694  vaddr 0x00254694   -> FUN_0025468c   (the node DEFINITION lookup: base + index*0x1c)
0x08A4EF70  vaddr 0x0024AF70   -> FUN_0024aee0   (the render loop, 4,776 bytes)
0x08A4EF8C  vaddr 0x0024AF8C   -> FUN_0024aee0
0x08A4EFC4  vaddr 0x0024AFC4   -> FUN_0024aee0
0x08A4F004  vaddr 0x0024B004   -> FUN_0024aee0
```

**This is a coherent picture, and all three routines are ones already identified independently:**

* `FUN_0024aee0` is the **render loop** (section 61) -- the function that reads each node and calls the
  render writer with an index. It writes node fields on every redraw, so hits there are expected and
  confirm the watchpoint is seeing the right structure.
* `FUN_0025468c` is the **definition lookup** `base + index * 0x1c` (section 39) -- it reads the node's
  index and resolves the definition record.
* `FUN_0024932c` is the **node-list manager** (sections 39, 55) -- counters and the free pool.

So the node `0x08C09344` is written during a verified `down` press **by the render loop and the definition
lookup path**, which is exactly the machinery that would repaint a list. That is the same subsystem the
section-61 recipe lives in.

### What this adds, precisely

* **The watchpoint method now works end-to-end with accumulation**: 6 hits across 6 presses, six distinct
  PCs, with the CPU resumed between them. Section 67's starvation defect is resolved.
* **Three routines are implicated by live writes during a verified press**: the render loop, the definition
  lookup, and the list manager. All three were previously identified from static analysis alone; this is
  their first dynamic confirmation as *writers* of menu-node state.
* **The `0x1c`-stride table and node N are NOT written during the press** -- two bounded negatives.

### What it does not add

It does not isolate the selection index. Six PC values, one per press, from three different routines, is
consistent with a **repaint** (every node field touched as the list redraws) rather than with a single
write of a single index. The distinguishing test is the one section 68 named and this run did not perform:
watch **one candidate index field** and require the *same* PC to write it on **every** press, rather than
collecting one hit per press from several routines.

### Method note

> **Resume before the press, not after the hit.** The two targets that produced zero hits did so because
> the loop explicitly resumed the CPU before each press; had it not, a frozen world from the previous
> target's hit would have made them look negative too. Ordering the resume *before* the stimulus is what
> makes a null meaningful.

> **Map every hit PC into the function map you already have.** All six PCs resolved to routines identified
> sections earlier -- the render loop, the definition lookup, the list manager. That turns "six opaque
> addresses" into "the list-repaint path", and it cost no new decompilation.

> **One hit per press across several routines suggests a repaint, not an index write.** A selection write
> should be the *same* PC every time. Requiring repetition of the PC -- not just any hit -- is the next
> discrimination, and it is the difference between watching a structure and watching a field.

