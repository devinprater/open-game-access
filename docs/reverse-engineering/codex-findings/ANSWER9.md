# ANSWER9 — lock target and EX-core objects (static analysis only)

All executable addresses are Ghidra ELF virtual addresses.  Fixed RAM addresses use the
validated `+0x08804000` relocation from ANSWER8B.  No emulator, ROM, save state, or asset
extraction was used.

## Result and important limit

The current aiming/lock target is a field of the local fighter object:

```text
M       = read32(0x08B955A0)
P       = read32(M + 0x14)          // local/first fighter in the validated fight
enemy   = read32(P + 0x2F0)         // fixed paired opponent
target  = read32(P + 0x2EC)         // nullable current/alternate lock target
```

The three observable states have this structural representation:

```text
target == enemy                     enemy lock
target == 0                         lock off
target != 0 && target != enemy      alternate battle-object target
```

Given the player-confirmed L1 ring `enemy -> EX core -> off`, the alternate object in that
three-state ring is the EX core.  That last English label is an inference from the supplied
mechanics plus the independently proven pointer states; the executable references establish
“alternate non-enemy target,” not a standalone symbol or type name reading `EX core`.

Two independent static references support the field and state interpretation:

* `FUN_000B64F8` is the enemy-lock writer: it sets `P+0x2EC = P+0x2F0` while setting the
  corresponding lock/status bits.  `FUN_000B6550` is the off writer: after clearing the enemy
  lock bits, it zeros `P+0x2EC` when no alternate-target condition remains.
* `FUN_0000D568` and `FUN_0000D880` independently use `P+0x2EC` when non-null and otherwise
  fall back to `P+0x2F0` for aiming/camera target transforms.  `FUN_0000DFB0` repeats the same
  preference for attack-direction calculation.  Thus `+0x2EC` is not the fixed opponent
  link or a presentation-only mirror.

`P+0x55C` points to the fighter's target/control state object.  Its `+0x104` mode is consumed
by `FUN_000B7CFC`: mode 0 calls the clear routine and mode 1 calls the enemy-lock routine;
`FUN_000A8AA8` independently changes this mode and invokes the old/new mode callbacks.
Those fields explain the transition machinery, but the adapter should read `P+0x2EC` itself:
it is the target actually consumed by aiming code and does not require decoding transient
mode/callback state.

## Generic battle-object list (where a spawned core can live)

The same battle-object manager owns a second list distinct from the fighter list:

```text
object_head = read32(M + 0x0C)
object_tail = read32(M + 0x10)
next(O)     = read32(O + 0x490)
kind(O)     = read8(O + 0x530)       // common update-order/class byte, values 0..4
position(O) = float O+0x80, O+0x84, O+0x88
```

Static references:

* `FUN_000B1020` is the generic-list accessor: null input returns `[M+0x0C]`, otherwise it
  returns `[O+0x490]`.  `FUN_000B05E0`, `FUN_000B0848`, and `FUN_000B0F90` independently walk
  exactly that chain for battle-object updates.
* `FUN_0009EB98` inserts common battle objects into `M+0x0C/M+0x10`, links them through
  `O+0x490`, and sorts them by byte `O+0x530`.  `FUN_000B0954` independently unlinks objects
  from the same chain during retirement and moves them to the manager's retirement list.
* The common-object initializer `FUN_000A485C` initializes XYZ at `O+0x80/+0x84/+0x88` and
  the class byte at `O+0x530`.  Independently, target consumers such as `FUN_000A1888` form
  vectors from `target+0x80`, while `FUN_0000D880` compares `target+0x84` with the fighter's
  Y coordinate.  These uses apply to the non-enemy target as well as a fighter target.

Therefore the lock pointer itself supplies the core object and its position without needing
to guess a subtype:

```text
if target != 0 && target != enemy:
    core = target
    core_xyz = floats(core + 0x80, core + 0x84, core + 0x88)
```

The distance required by the beacon is the Euclidean length of `core_xyz - player_xyz` (or
the enemy equivalent).  Both object classes use the same world-position offsets.

## What is not statically established

No defensible fixed `kind(O)` value for “EX core” was found.  The byte at `O+0x530` is a broad
common-object class/update-order value, not a semantic item id: constructors use values 0
through 4 for fighters and several unrelated common object families.  Calling any one value
“EX core” would be invented.

Likewise, no independently supported claimed/consumed byte or bit was found.  The generic
objects have several large common status bitfields (`+0x370` through `+0x37C`), and
`FUN_000B0954` retires objects based on common lifecycle masks, but the executable-only
evidence does not map one of those bits specifically to core ownership/consumption.  The
safe observable is lifecycle membership: an available target is a non-null alternate pointer
which is still present in the `M+0x0C/+0x490` list; after consumption/retirement it is removed
and the fighter target changes or clears.  Do not continue dereferencing a cached pointer once
it disappears from the current list.

This is intentionally narrower than claiming that every entry in the generic list is a core.
Projectiles, effects, and other common battle objects also use that list and layout.  A full
enumeration of all unselected spawned cores requires a second semantic discriminator from
the battle assets or a future independently validated writer/type test; static executable
analysis here does not supply one.

## Minimal validator reads

### Lock state and beacon target

1. Resolve `M=[0x08B955A0]`, enumerate fighters from `[M+0x14]` by `+0x4EA8`, and select the
   local fighter by the already validated adapter rule rather than blindly assuming list order
   in every mode.
2. Read `enemy=[P+0x2F0]` and `target=[P+0x2EC]`.
3. Classify: zero = off; equal to `enemy` = enemy; nonzero and unequal = alternate/core under
   the validated three-state battle contract.
4. For a nonzero target, read finite floats at `target+0x80/+0x84/+0x88`; compute distance from
   the finite player floats at the same offsets.
5. For an alternate target, additionally walk `[M+0x0C]` via `+0x490` with a bounded count and
   require that `target` occurs in the current list before exposing it or caching it.

### Spawned-object/lifecycle validation

1. Read `head=[M+0x0C]`, `tail=[M+0x10]`; walk `next=[O+0x490]` with pointer, alignment,
   cycle, and maximum-count guards.
2. For each entry read `u8[O+0x530]` and finite XYZ at `O+0x80/+0x84/+0x88`.  Treat the kind
   byte as diagnostic only, not as a core label.
3. Identify the currently selected core only by equality with the validated alternate
   `P+0x2EC` pointer.  Do not label the remaining generic entries as cores.
4. Treat disappearance from the current list plus target change/clear as retirement/consumption.
   There is no validated standalone `claimed` offset to read.

Confidence: `P+0x2EC` current target, `P+0x2F0` fixed enemy, null/equal/alternate structural
states, generic list `M+0x0C` with link `+0x490`, and XYZ `+0x80/+0x84/+0x88` are **verified
statically with multiple references**.  The semantic identification of the alternate state
as EX core relies additionally on the player-supplied three-state contract.  A universal
EX-core subtype and a claimed/consumed flag remain **unresolved**, and no offsets were
fabricated.
