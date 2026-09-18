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

