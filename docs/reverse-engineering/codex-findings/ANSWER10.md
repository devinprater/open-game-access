# ANSWER10 — stage geometry and surroundings (static analysis only)

All executable addresses are Ghidra ELF virtual addresses. Fixed RAM addresses use the
validated `+0x08804000` relocation from ANSWER8B. No emulator, ROM, save state, or stage
asset extraction was used.

## Result

The executable establishes where the loaded stage package is dispatched, but it does **not**
leave walls, cover, ramps, and BRV-zero traps in one enumerable, semantically typed array.
The stage loader hands geometry-like chunks to opaque engine objects and hands other chunks
to gameplay/environment services. Consequently, there is no statically validated
`nearest-cover`/`nearest-ramp` API in this build, and no universal trigger type that the
executable names “BRV zero.”

The safe implementation split is:

1. use an offline per-stage geometry/profile index for wall, cover, and ramp speech; and
2. enumerate live common battle objects only for dynamically spawned/scripted objects after a
   stage-specific discriminator has been validated.

Calling an arbitrary table in the services below “the collision triangles” or assigning an
English feature name to an id would go beyond the static evidence.

## Loaded stage holders

`FUN_0006F3F4` parses the non-resident stage container. `FUN_000E5A64` searches its 0x10-byte
directory entries:

```text
container+0x04  s32 entry_count
container+0x08  relocated container base (installed by FUN_000E5960)

entry stride    0x10
entry+0x00      u16 class
entry+0x04      u32 four-byte tag
entry+0x08      data pointer/offset
entry+0x0C      byte size
```

`FUN_000E5960` independently proves the 0x10 stride and relocates each non-null `entry+0x18`
relative to the base; `FUN_000E5A64` independently walks the same count/stride and matches
class, tag, and occurrence. Thus the loader's `iVar1+8/+0x0C` arguments are a chunk address
and size, not a geometry-list pointer and count.

The relevant live roots are:

```text
RAM 0x08B963C0 = DAT_003923C0       embedded world/render service object
RAM 0x08B95230 = DAT_00391230       embedded three-handle/aux-record holder
RAM 0x08805AAC = FUN_00030A18()     embedded parsed gameplay-field database
RAM 0x08805E48 = FUN_00066314()     embedded environment/common-object services
RAM 0x08B955E0 = DAT_003915E0       resource manager
```

These are embedded objects, not pointer slots. That distinction matters for validator reads.
`FUN_00030A18` constructs the object at vaddr `0x1AAC`, while `FUN_00066314` constructs the
object at vaddr `0x1E48`; both return those object addresses directly. Their independent
constructors, `FUN_0003083C` and `FUN_00065B28`, initialize many subobjects at fixed offsets.

The stage loader dispatches chunks as follows:

* `DAT_003923C0+0x180` through `FUN_000E1C04`, `+0x790` through `FUN_000E4520`, and five
  decoded pieces through `FUN_000E2600(DAT_003923C0+0x4E0, slot, data, size)`;
* `DAT_00391230+0/+4/+8` through `FUN_000722FC`, and `+0x0C` through `FUN_0007229C`;
* `FUN_00030A18()` through `FUN_00030970` and `FUN_000309A0`;
* `FUN_00066314()+0x60` through `FUN_00065EA0` and `+0xA8` through `FUN_00065F18`;
* resource ids `0x7F`, `0x80`, `0x81`, and `0xC1` through `FUN_000B2B84`.

Independent teardown evidence is `FUN_0006F2D0`, which clears the same services and removes
the same four resource ids. This validates ownership/lifetime, but not English feature
semantics.

### What layouts are actually recoverable

The `FUN_00030A18()` object is a parsed field/gameplay database, not a proven triangle mesh.
Its main loader `FUN_00058C6C` repeatedly builds `{s32 count, pointer}` pairs at offsets
`+0x18/+0x1C`, `+0x24/+0x28`, ... through `+0x27C/+0x280`, with per-group strides supplied
to `FUN_00030A68`. Examples include:

```text
base+0x144  s32 count
base+0x148  pointer to records of stride 0x114

base+0x0E4  s32 count
base+0x0E8  pointer to 8-byte records
base+0x0F0  s32 count
base+0x0F4  pointer to 4-byte records

base+0x1CC  s32 count
base+0x1D0  pointer to 8-byte records
base+0x1D8  s32 count
base+0x1DC  pointer to 0x28-byte records
```

This layout has two independent references: `FUN_00058C6C` creates the pairs with explicit
strides, while accessors such as `FUN_00057B6C`, `FUN_000575CC`, and `FUN_000580F0` bound
their walks by those counts and use strides `0x114`, `4`, and `0x28` respectively. Those
accessors are keyed lookups used by combat/AI rules; none accepts a world position and
returns a nearest geometric primitive. Therefore this database must not be exposed as a
wall/cover/ramp list.

`DAT_00391230+0x10` does point to records of stride `0x4C`: `FUN_000717F8` selects three such
records using indices obtained from a fighter's model/control data. Independently,
`FUN_00071C20`/`FUN_00072C1C` consume the holder's `+4/+8` handles in rendering work. This is
an auxiliary per-model/render structure, not evidence for stage collision shapes.

The geometry-like chunks stored under `DAT_003923C0` are opaque engine handles created by
`FUN_0034D29C`/`FUN_0034D57C`. The executable does not expose their internal vertex/face
layout. Accordingly, no defensible live `count + triangle pointer`, wall normal array, or
cover/ramp shape layout can be supplied from this executable alone.

## Nearest-feature query

No call reachable from the stage loaders implements a general
`(world_xyz, feature_class) -> nearest feature` query. This is a negative result supported by
two independent call families:

* the `FUN_00030A18()` consumers use id-to-record lookups (`FUN_00057B6C`,
  `FUN_000575CC`, `FUN_000580F0`) rather than spatial searches; and
* the `FUN_00066314()` consumers either test environment state or iterate common objects.
  For example `FUN_00061FE0` walks `{count at +4, pointer-array at +8}` and checks
  `FUN_000612D4(object)==2`; `FUN_0006233C` independently walks the same array and compares
  object positions at `+0x80` to a supplied position. It does not return a nearest distance
  or identify walls, ramps, or cover.

Thus the cheapest **validated** route is not to walk the dozens of field-database groups.
Build a small per-stage spatial index from legally available stage geometry/profile data
offline, keyed by the loaded stage id, then query that index from the player's validated
`P+0x80/+0x84/+0x88` position. Recommended derived classifications are explicitly
inferences:

* wall/boundary: nearest non-walkable boundary segment;
* ramp: walkable surface whose normal/slope crosses a chosen threshold and connects two
  height bands;
* cover: solid obstacle that blocks a horizontal segment from player to opponent and has a
  reachable near side.

For a small arena a linear scan is already cheap; otherwise use a 2-D X/Z uniform grid or
BVH. Report horizontal distance and bearing for speech, and separately report elevation for
ramps. Without an offline stage profile, the executable-only fallback can say “geometry
unavailable”; it cannot honestly label the opaque handles.

## Traps and gimmicks

The environment service at `FUN_00066314()` owns live common-object facilities. Its
`+0x1B8` subobject has this validated enumerable layout:

```text
E       = 0x08805E48
L       = E + 0x1B8
count   = read32(L + 0x04)
items   = read32(L + 0x08)       // pointer array, count entries
O       = read32(items + 4*i)
class   = FUN_000612D4(O)        // value 2 is explicitly selected by consumers
XYZ(O)  = floats O+0x80, O+0x84, O+0x88
```

`FUN_00061FE0` and `FUN_0006233C` independently enumerate precisely that count/pointer array
and select class `2`; `FUN_0006233C` independently consumes `O+0x80` as its position. This
is useful as a bounded live-object candidate list, but class `2` is **not statically proven
to mean trap**. ANSWER9 also showed that ordinary spawned battle objects share XYZ at
`+0x80/+0x84/+0x88`, so position alone is not a discriminator.

The collision/interaction path `FUN_00013800` does prove that scripted common-object contact
can dispatch several outcome classes: it derives outcome values with `FUN_000BE1F8` and
`FUN_000BE2CC`, then routes values `1/2/5`, `4`, and `3` through different handlers including
`FUN_000665BC` and `FUN_000E2898`. Independently, `FUN_00012824` routes contact through
`FUN_000664B4` or `FUN_00066538` depending on a record flag. These establish trigger/contact
machinery, but neither path maps one outcome or record id specifically to “set Bravery to
zero.”

Therefore a BRV-zero trap is a stage-specific semantic pairing of a live trigger/object and
the already validated BRV change at `[P+0x51C]+0x0E`. No universal trigger-record offset,
type id, radius field, or damage-class id is supported by two static references. The safe
validator is differential: correlate a candidate object's proximity/contact with the
authoritative BRV transition, then persist that discriminator only for the tested stage.

## Minimal validator reads

These reads are sufficient to distinguish valid holders from fabricated layouts within a
live battle, without scanning RAM:

1. Read `E=0x08805E48`. At `L=E+0x1B8`, read `count=[L+4]` and `items=[L+8]`. Require a small
   nonnegative count, aligned RAM pointer, and bounded pointer-array walk. For each non-null
   object read finite XYZ at `O+0x80/+0x84/+0x88`. Do **not** label entries as traps yet.
2. Read field database `F=0x08805AAC`. Check several independent pairs, especially
   `[F+0x144]/[F+0x148]`, `[F+0x0E4]/[F+0x0E8]`, and `[F+0x1D8]/[F+0x1DC]`. Counts should be
   nonnegative and nonzero pointers aligned/in RAM. This validates the loaded parsed
   database, not geometry semantics.
3. Read `DAT_00391230` at RAM `0x08B95230`: inspect handles at `+0/+4/+8`, copied-data pointer
   at `+0x0C`, and record pointer at `+0x10`. If `+0x10` is non-null, sample consecutive
   `0x4C` records only as a structural check; do not call them walls.
4. Read the world-service root at `0x08B963C0` and record the handles at `+0x180` and `+0x790`.
   Their being non-null during battle and clearing at teardown validates lifetime only;
   dereferencing them as a guessed mesh is unsafe.
5. Resolve the participant chain from ANSWER8B and read player XYZ plus
   `BRV=s16[[P+0x51C]+0x0E]`. During a known trap contact, capture only the bounded candidate
   list above and the BRV before/after. A candidate classification becomes stage-specific
   evidence only after repeatable spatial/time correlation; a single matching value is not
   enough.

## Confidence and boundary

The container directory, service roots, field-database count/pointer pairs and strides,
environment-object enumeration, and common XYZ offsets are **verified statically with
independent references**. A universal collision-mesh layout, nearest-feature API, cover/ramp
ids, and BRV-zero trigger id are **not established by this executable**. Those semantic
labels require stage asset/profile analysis or a narrowly scoped live differential; none was
invented here.

## TASK10B correction — segment-relative singleton addresses

The original answer treated the decompiler constants `0x1AAC` and `0x1E48` as complete ELF
virtual addresses. They are not. `FUN_00030A18` and `FUN_00066314` are code at ELF vaddrs
`0x00030A18` and `0x00066314`; the constants returned by them are **segment-1-relative
addends** for singleton objects in the second `PT_LOAD`, not addresses in the code segment.

The second load segment has ELF vaddr `0x003A6860`. Resolving the addends gives:

```text
field singleton:       0x003A6860 + 0x1AAC = ELF vaddr 0x003A830C
environment singleton: 0x003A6860 + 0x1E48 = ELF vaddr 0x003A86A8
```

Both lie in `.bss`, which begins at ELF vaddr `0x003A8300`. The associated one-time guards
are immediately before them: `0x003A8308` and `0x003A86A4`.

This is supported independently in two ways. First, the PSP relocation entries on each
function's `lui`/low-half address-building instructions select target segment 1
(`r_info` values `0x00010005` and `0x00010006`), so Ghidra's raw `0x1AAC`/`0x1E48`
rendering is missing the segment-1 base. Second, `FUN_0003083C` initializes fields through
`object+0x2A4` and `FUN_00065B28` initializes the corresponding environment subobjects,
while their many independent consumers reuse the values returned by the singleton
accessors. They are writable objects, consistent with the normalized `.bss` locations and
inconsistent with the low addresses inside the executable code segment.

Using the two candidate biases explicitly:

| Item | ELF vaddr | `+0x08800000` | `+0x08804000` |
|---|---:|---:|---:|
| `FUN_00030A18` code | `0x00030A18` | `0x08830A18` (code mapping) | `0x08834A18` |
| field singleton `F` | `0x003A830C` | `0x08BA830C` | **`0x08BAC30C`** |
| `FUN_00066314` code | `0x00066314` | `0x08866314` (code mapping) | `0x0886A314` |
| environment singleton `E` | `0x003A86A8` | `0x08BA86A8` | **`0x08BAC6A8`** |

The bold addresses are the live object addresses under the independently validated data
bias. In contrast, `0x08805AAC` and `0x08805E48` result from adding the data bias directly
to unresolved segment-relative addends; they are not these objects, hence the garbage.
Likewise, `0x08801AAC` and `0x08801E48` merely add the code bias to those unresolved
addends. Their zeros are unrelated low-address contents, not valid empty singleton state.

### Corrected minimal validator reads

Use the data bias only after converting each segment-relative addend to its full ELF vaddr:

```text
F = 0x08BAC30C                         // 0x003A830C + data bias
read32(F + 0x0E4), read32(F + 0x0E8)  // 0x08BAC3F0, 0x08BAC3F4
read32(F + 0x144), read32(F + 0x148)  // 0x08BAC450, 0x08BAC454
read32(F + 0x1D8), read32(F + 0x1DC)  // 0x08BAC4E4, 0x08BAC4E8

E = 0x08BAC6A8                         // 0x003A86A8 + data bias
L = E + 0x1B8 = 0x08BAC860
count = read32(0x08BAC864)             // L + 4
items = read32(0x08BAC868)             // L + 8
```

Retain the original bounds checks: counts must be small and nonnegative; a pointer required
by a nonzero count must be aligned and in RAM; and any object walk must be capped by the
validated count. Zero count/null pointer pairs can be legitimate for unloaded or empty
groups and do not establish that an address is correct by themselves.

The honest-negative result is unchanged. The field singleton's count/pointer groups are
supported by loader plus accessor references, and the environment `+0x1B8` list is supported
by the two enumerators already cited. Those references establish structure and lifetime,
not English meanings such as wall, cover, ramp, trap, or BRV-zero trigger. No such semantic
label should be emitted without separate stage-specific evidence.
