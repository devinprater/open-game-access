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

