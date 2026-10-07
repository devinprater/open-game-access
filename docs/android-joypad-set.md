# Android's `bizhawk_compat.lua` has an inert `joypad.set` — the modifier layer's release latch cannot engage

Measured 2026-10-07 while restructuring the reader set. This is a **real defect**,
not a cosmetic platform difference, and it is the reason the top-level Android
`bizhawk_compat.lua` was left alone in the restructure rather than overwritten.

## The two copies

| | path | `joypad.set` |
|---|---|---|
| iOS loads | `Sources/OpenGameAccess/Resources/bizhawk_compat.lua` | works — calls `input.JoySet()` |
| Android loads | `app/src/main/assets/lua/bizhawk_compat.lua` | **empty function, does nothing** |

Android's loader names its own copy explicitly
(`AccessibilityScript.kt:35`: `private const val SHIM_ASSET = "lua/bizhawk_compat.lua"`),
so this is not a staging mistake — it is the file Android actually uses.

## Why the stub is not harmless

`main.lua` runs a controller-modifier layer: while a trigger is held it writes
every button as `false` (blocking game input) and drives the accessibility list
instead. It re-latches from the physical pad with:

```lua
joypad.set({})                          -- drop our overrides + re-latch from physical
local phys = joypad.getimmediate()      -- now the TRUE physical state
```

`joypad.set({})` is **not** a no-op — an empty table must CLEAR the overrides. The
canonical copy does exactly that via `input.JoySet({})`. The Android copy is:

```lua
function joypad.set() end
```

so the overrides are never cleared. The in-file comment on the canonical version
states the consequence: *"Making this an empty function (as the first mobile port
did) leaves the release latch unable to engage, so a direction still held when the
trigger is let go leaks through to the game as an unasked-for step."*

`main.lua` calls `joypad.set` **16 times**, including the `pad_dirty` idle-clear
path at :301 and the re-latch at :305.

## But the fix is not simply to copy the canonical file

`input.JoySet` is registered in **`Core/pokecore.cpp`** (`LuaJoySet`, registered in
the `input` table at :680) — the shared core. Android does **not** link that core:
it embeds melonDS-android plus `MGBACore.cpp`/`MGBARunner.cpp`/`MGBAScriptJNI.cpp`
and `PokeScript.cpp`. Grepping the whole Android native + Java tree for `JoySet`
finds only unrelated `setJoypadState(uint32_t)` C++ methods — **no Lua `input.JoySet`
binding exists on Android at all**.

The canonical file is guarded (`if input.JoySet then`), so copying it to Android
would be *safe but still inert* — it would behave identically to the stub until
Android exposes the binding. That guard is why the two files look like a
deliberate platform split when they are really "iOS has the binding, Android
does not".

## What Android needs

1. **Expose a Lua `input.JoySet(table)` binding in the Android Lua host** — the
   equivalent of `LuaJoySet` in `Core/pokecore.cpp`. `PokeScript.cpp` is the NDS
   path's Lua host and its header already carries `setJoypadState`; the binding is
   the missing piece.
2. **Then** make Android's `bizhawk_compat.lua` identical to the canonical copy, or
   better, delete it and let the reader set be single-sourced there too (it is the
   last duplicate of a reader file — `main.lua` is byte-identical, so only this one
   genuinely differs).

Until step 1 lands, **a direction held while the modifier trigger is released can
leak through to the game**, which is a gameplay-input defect, not a missing
feature.

## Provenance note

Git records both copies as last touched by the same commit (`683e43b`), which is
what a copy-paste looks like, and the Android copy's own comment describes the
empty function as "the first mobile port's" approach — i.e. the author knew the
correct version and the Android file did not receive it.
