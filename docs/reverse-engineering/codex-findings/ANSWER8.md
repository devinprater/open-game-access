# ANSWER8 — battle participant state (static analysis only)

All addresses below are EBOOT virtual addresses unless explicitly labelled PSP RAM.  For this
build the usual load bias is `+0x08800000`, so `DAT_003915A0` is at RAM `0x08B915A0`.
No emulator, save state, or ROM/asset extraction was used.

## Result

The live fighters are not the `0x09D8Exxx` render records from the differential.  They are large
heap objects linked from the battle-object manager:

```text
manager = *(u32 *)0x08B915A0
P0      = *(u32 *)(manager + 0x14)       // head of fighter/active-character list
P1      = *(u32 *)(P0 + 0x2F0)           // P0's paired opponent/target in a 1-v-1 fight
next(P) = *(u32 *)(P + 0x4EA8)           // enumerate list; do not assume only two entries
S(P)    = *(u32 *)(P + 0x51C)            // gameplay-stat block
```

For the supplied WoL-versus-Garland story fight, `P0` and `P1` are the two combatants.  Code
which must be generic should enumerate the `+0x14/+0x4EA8` list and retain character objects,
rather than assuming the list can never contain an assist or other active character.

Evidence (independent references):

* `FUN_000B1040` returns `[DAT_003915A0+0x14]` on its first call and `[P+0x4EA8]`
  thereafter.  `FUN_000B1060` independently walks precisely the same chain and counts it.
* The character constructor/inserter at `0x000BAF9C` appends `P` through manager offsets
  `+0x14/+0x18` and writes the next link at `P+0x4EA8`; `FUN_000B6150` independently
  unlinks the identical fields during destruction.
* Combat calculations repeatedly treat `P+0x2F0` as the paired character.  For example the
  function containing `0x0005E444` compares both characters' stat blocks, while
  `FUN_0002C248` obtains the vertical separation as `[P+0x2F0+0x84]-[P+0x84]`.

## Stats

The fields are in `S`, not directly in `P`:

| Meaning | Type / expression | Offset |
|---|---:|---:|
| HP maximum | `u16` | `S+0x08` |
| HP damage accumulated | `u16` | `S+0x02` |
| HP current | `max(0, (s16)[S+0x08] - (s16)[S+0x02])` | derived; no independent current field |
| Bravery current | `s16` | `S+0x0E` |
| Bravery base/normal cap | `s16` | `S+0x10` |
| EX gauge current | `float`, clamped `0.0..10000.0` | `S+0x14` |
| EX full | `[S+0x14] >= 10000.0` plus eligibility checks | derived; no dedicated full byte/word found |

Do not expose `S+0x02` as current HP: it rises when damage is taken.  The engine consistently
computes current HP as maximum minus that field.

Static cross-checks:

* `FUN_000B1D60` adds damage to `S+0x02`, records the hit in `+0x04/+0x06`, and compares it
  against `S+0x08`.  `FUN_000B1B78` independently computes `max-damage` and applies an HP
  modifier without letting the result exceed max minus one.
* The calculations around `0x0005E444` and `0x0006EF90` independently read current BRV from
  `S+0x0E`; the former also reads `S+0x10` as the normal/base value.  The test around
  `0x0002C9xx` explicitly checks `S+0x0E >= S+0x10`.
* `FUN_000B1B78` adds an EX modifier to `S+0x14` and clamps it to 0/10000.  The command/effect
  handler near `0x0008xx` independently changes the same float and performs the same clamp.
  `FUN_000BA0C8` is the strongest full-EX reference: it tests `10000.0 <= [S+0x14]`, then
  applies battle-mode/status eligibility tests.  Thus a purported stored “full flag” offset
  would be invented; the game derives it.
* `FUN_000A0FF0` copies level/HP-damage/EX from `S` into a small presentation/save mirror at
  `P+0x2FC`.  This is further evidence that nearby duplicate values are mirrors, not the
  authoritative stat block.

## Position and bearing

World position is directly in each fighter object:

```text
X = *(float *)(P + 0x80)
Y = *(float *)(P + 0x84)
Z = *(float *)(P + 0x88)
```

Use `P1-P0` for the bearing vector and Euclidean length (or horizontal X/Z length if speech
should ignore elevation).  `FUN_00000BF8` independently copies `P+0x80/+0x84/+0x88` into an
effect's XYZ fields; the opponent-height routine at `0x0002C248` subtracts the two `+0x84`
values through `P+0x2F0`, and several collision/placement routines compare the same offsets.

## Stage hazards, cover, and ramps

These are **stage data, not fields in either participant**.  The statically defensible source
is the loaded stage package and its collision/gimmick services:

* `FUN_0006F3F4` parses the stage container with `FUN_000E5A64`, dispatches several chunks to
  world/collision services (`DAT_003923C0`, `DAT_00391230`, and the managers returned by
  `FUN_00030A18` / `FUN_00066314`), and registers resource records `0x7F`, `0x80`, `0x81`,
  and `0xC1` in the resource manager at `DAT_003915E0`.
* `FUN_000B2D38` independently resolves those registered records by their signed 16-bit id;
  `FUN_0006F2D0` removes the same four records at stage teardown.

Consequently:

* ramps and solid cover must be derived from/query the stage collision geometry;
* destructible or scripted cover/traps come from stage gimmick/object records;
* a “BRV-zero trap” is a semantic combination of a trigger/gimmick and battle effect, not a
  universal fixed field or object type in `P`.

The executable alone does not contain a verified universal table mapping geometry/object ids
to the English labels “cover”, “ramp”, or “BRV-zero trap”.  Giving such ids or per-object
offsets without the stage asset would violate the no-invented-offset rule.  An adapter should
use collision queries for traversability/cover and stage-gimmick enumeration for damaging
triggers; this is a separate map module from participant speech state.

## Classification of the four persistent drops

**None of the four reported addresses is authoritative participant gameplay state.**

| Candidate | Static classification |
|---|---|
| `0x09D8EBC8` (duplicate at `0x09D8E944`) | presentation/render mirror.  Its pointer/floats and duplicated 32-bit `243` do not match the authoritative `s16 S+0x0E` layout. |
| `0x09B27974` (`512→256→0`) | timer/countdown candidate, not a participant stat: it is a 32-bit stepped value and no supplied pointer chain connects it to `P` or `S`. |
| `0x09D75B78/+7C` (`1274→0`) | transient pair/event state, not the nested stat block.  The paired 32-bit layout conflicts with HP/BRV/EX types above. |
| `0x08BAC79C` (`350→340→300`) | global/static slow timer candidate.  It is outside the heap fighter/stat chain and has the wrong update/type signature. |

The decisive validation is structural, not whether a number happens to resemble BRV: resolve
`P0`, `P1`, then `S(P)` and read the typed fields above.  In particular a renderer may cache
BRV as a 32-bit integer at more than one address, exactly explaining `755→243` and its
duplicate, while combat logic reads/writes the single signed halfword at `S+0x0E`.

## Minimal validator reads

1. Read `M=[0x08B915A0]`, `P0=[M+0x14]`, `P1=[P0+0x2F0]` and verify that walking
   `P0+0x4EA8` reaches the paired active character (or enumerate if additional actors exist).
2. For each `P`, read `S=[P+0x51C]`; reject null/misaligned pointers.
3. Read `u16 S+8`, `u16 S+2`, `s16 S+0x0E`, `s16 S+0x10`, and float `S+0x14`.
4. Require `0 <= HPcurrent <= HPmax` and `0.0 <= EX <= 10000.0`; full EX is the derived
   threshold, not a memory flag.
5. Read XYZ floats at `P+0x80/+0x84/+0x88` and sanity-check finite values.

Confidence: participant holder/list, nested stat pointer, HP representation, BRV offsets, EX
offset/threshold, and XYZ offsets are **VERIFIED STATICALLY with multiple references**.
English semantic classification of individual stage-object ids remains **UNRESOLVED without
stage asset data**, and no ids were fabricated.

## TASK8B correction — holder address and fighter-list writer

The list structure was identified correctly, but the fixed RAM address in the original answer
was not.  This Ghidra project uses ELF virtual addresses whose RAM relocation is
`+0x08804000`, as independently demonstrated by the already-live board holder
`0x00394940 -> 0x08B98940`.  Therefore the corrected chain is:

```text
manager_slot = 0x08B955A0                 // vaddr 0x003915A0 + 0x08804000
M            = read32(manager_slot)       // normally 0x08854930
P0           = read32(M + 0x14)
next(P)      = read32(P + 0x4EA8)
P1           = read32(P0 + 0x2F0)         // paired opponent, not required for enumeration
S(P)         = read32(P + 0x51C)
```

Thus the zero page at `0x08B915A0` was a read 0x4000 bytes below the real static slot; it is
not evidence for another battle manager.  `FUN_000FA84C` supplies the first independent static
reference: after one-time construction by `FUN_000B037C(&DAT_00050930)`, it assigns
`DAT_003915A0 = &DAT_00050930`.  The fighter constructor and both list accessors independently
dereference that same `DAT_003915A0` value.  In RAM, the slot should consequently contain
`0x08854930` (`0x00050930 + 0x08804000`), rather than itself being the manager object.

The earlier attribution of insertion to `FUN_000BAF9C` was also wrong.  `FUN_000BAF9C` is a
per-fighter update routine (its sole direct caller is `FUN_000BBF98`); it does not insert the
object.  The actual constructor/inserter is `FUN_000BD3E4`.  Near its end it performs the
authoritative writes:

```text
if (M->tail == 0) {
    M->head = P;                            // M+0x14
    M->tail = P;                            // M+0x18
} else {
    M->tail->next = P;                      // old tail +0x4EA8
    M->tail = P;
}
P->next = 0;                               // P+0x4EA8
P->list_index = FUN_000B1060();            // P+0x314
```

Independent structural confirmation comes from `FUN_000B6150`, which removes a fighter using
the same `M+0x14`, `M+0x18`, and `P+0x4EA8` fields, and from `FUN_000B1040` plus
`FUN_000B1060`, which respectively enumerate and count exactly that chain.

The ordinary battle initialization path is:

```text
FUN_00120188
  -> FUN_00122CCC(DAT_003931A0)             // main battle setup
     -> FUN_001224C4(battle_state)
        -> allocate 0x4ED0-byte fighter with FUN_0009C864
        -> FUN_000BD3E4(P, controller, character_data, side)
```

`FUN_001224C4` contains five direct constructor calls and covers all three branches of the
battle-state mode at `battle_state+0x4C`: mode `3`, mode `4`, and the default/other branch.
Mode 3 constructs both sides in its own branch; mode 4 has a special first-side selection;
the default branch has the normal first-side selection; all paths construct the second side.
Every one of those calls ends in the same `DAT_003915A0` list insertion.  As a second reference
to the setup route, `FUN_00122CCC` calls `FUN_001224C4` directly at `0x00123EE8`, while its
wrapper `FUN_00120188` is called from the battle controller `FUN_0012076C`.

No constructor call or list operation in these paths selects an alternate holder, and no
battle mode among `3`, `4`, or default uses a different manager.  Accordingly, the correction
is the `+0x4000` address fix and the writer re-identification—not a mode-specific manager.
The stat, position, pairing, and link offsets in the original answer are unchanged.
