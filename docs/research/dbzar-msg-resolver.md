# DBZ Another Road — MSG-ID → TEXT RESOLVED (job B, solved)

**The message text is now readable.** 7,487 messages resolved out of `data_sys_us.afs` alone,
plus 160 in the ELF, all in English with the original line breaks. This was the open item
"resolve the MSG-ID→text path" from `DECOMP_INDEX.md §6`.

## The container format — documented by the game's OWN loader

`FUN_000da040` is the loader, and its decompile *is* the format spec:

```c
if (param_1[1]=='M' && param_1[2]=='S' && param_1[3]=='G') {
    if (*param_1 == '!') return 1;              // already loaded
    if (*param_1 == '#') {                      // not yet loaded
        *(char**)(p+0x14) = p + *(int*)(p+0x14);   // fix up: relative -> absolute
        *(char**)(p+0x18) = p + *(int*)(p+0x18);
        for (i = 0; i < *(u16*)(p+0x12); i++) {     // COUNT at +0x12
            v = *(int*)(*(int*)(p+0x14) + i*4);
            if (v) *(char**)(*(int*)(p+0x14)+i*4) = p + v;   // array 1 -> absolute
        }
        for (i = 0; i < *(u16*)(p+0x12); i++) {
            v = *(int*)(*(int*)(p+0x18) + i*4);
            if (v) *(char**)(*(int*)(p+0x18)+i*4) = p + v;   // array 2 -> absolute
        }
        *param_1 = '!';
    }
}
```

So the layout is:

| offset | type | meaning |
|---|---|---|
| `+0x00` | byte | `'#'` = not loaded, `'!'` = loaded (the load flag) |
| `+0x01` | 3 bytes | `"MSG"` magic |
| `+0x12` | u16 | **COUNT** |
| `+0x14` | u32 | **relative** offset → array[COUNT] of u32, each relative to base — **the NAMES** |
| `+0x18` | u32 | **relative** offset → array[COUNT] of u32, relative likewise — **the TEXT** |

**`text(id) = base + rel( array_at(base+0x18)[id] )`**, and `name(id)` the same via `+0x14`.

⛔ **The text is UTF-16LE, not ASCII.** Scanning for a single NUL truncates every string to one
character (the first attempt returned `b'M'` for a whole line) — that one-character result is the
giveaway. Decode as UTF-16LE.

## Results

| source | containers | messages |
|---|---|---|
| `EBOOT.dec` | 3 `#MSG` containers | 160 (system text, dialog words, help prompts) |
| `data_sys_us.afs` | **287** containers | **7,425** with real text |

Full dump: `%LOCALAPPDATA%\Temp\dbz-afs-msg-resolved.txt` (and `dbz-msg-resolved.txt` for the ELF).

### The story text, read out

Chapter narration, e.g.:

```
MSG_AR_000_00_000  "In another future…"
MSG_AR_000_00_001  "Goku suffered and died of a heart condition,\nand Earth's strongest
                    defenders fell at the hands of the androids."
MSG_AR_001_06_000  "In another future world,\nsomething unbelievable\nhas happened."
```

✅ **Live-confirmed:** a cutscene on screen showed *"In another future..."* and the resolver maps
it to `MSG_AR_000_00_000` at container `0x3C01790`, index 0. That is the resolver verified
against pixels, not just against itself.

The **24 chapter clear conditions** (`MSG_AR_CLEAR_00`..`23`) are now readable, e.g.
`"Defeat Dabura!"`, `"Defeat the mind-controlled Piccolo!"`, `"Gather 7 Dragon Balls before time
runs out!"`, `"Defeat Super Buu!"`, `"Defeat Kid Buu!"`. The **24 city/field names**
(`MSG_AR_CITY_00`..`23`) include `South Village`, `East Village`, `North Village`, `Central
Village`, `West Village`, `Dende's Village`. Chapter Select's own labels are
`MSG_AR_CHPTSEL_000..002` = `TOTAL`, `City DF. `, `complete`.

### Message-id structure (finally explicit)

| pattern | example | meaning |
|---|---|---|
| `MSG_AR_<chapter>_<scene>_<line>` | `MSG_AR_001_06_002` | story cutscene line: chapter 1, scene 06, line 2 |
| `MSG_AR_CLEAR_<nn>` | `MSG_AR_CLEAR_14` | the clear condition for chapter 14 |
| `MSG_AR_CITY_<nn>` | `MSG_AR_CITY_04` | city/field 04 = "East Village" |
| `MSG_AR_FIELDPLAY_<nnn>` | `MSG_AR_FIELDPLAY_004` | in-level strings (Senzu prompts, Dragon Ball finds, "Clear Condition", Yes/No) |
| `MSG_AR_AFBT_<n>` | `MSG_AR_AFBT_0` | 528 battle/victory quips ("That was disappointing.", "Haha! I did it!") |
| `MSG_DR_<nnn>_<A/B>_<nnn>` | `MSG_DR_071_A_000` | the **Japanese** drama script (still JP in the US build) |
| `MSG_MN_ZT_CH_<n>` | `MSG_MN_ZT_CH_0` | Z Trial mission conditions |
| `MSG_CMT_<n>` | | 93 comments |

⚠️ `MSG_DR_*` is Japanese even in this US release — the `data_sys_us.afs` drama script was not
translated. Worth knowing before wiring it to a reader.

## Tooling

| script | purpose |
|---|---|
| `dx_msgresolve.py` | resolves every `#MSG` container in the ELF |
| `dx_afsmsg.py` | resolves every `#MSG` container in `data_sys_us.afs` (the 7,425) |
| `dx_msgparse.py` | the first, narrower parser (kept: shows the truncation trap) |

## What this unblocks

A story reader can now **speak the actual dialogue and the clear conditions**, with no guesswork:
the id is in the message name, and the name is in the container. Remaining: find which id the
engine is *currently displaying* (the live message pointer), which is the next job for the story
reader.
