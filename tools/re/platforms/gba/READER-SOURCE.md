# Where the readers actually live, and why it matters

Discovered while checking an unrelated lead. The working copy I had been using was the
**wrong one**, and the real installation changes what the mGBA port needs.

## The two trees

| | real install | what I was using |
|---|---|---|
| path | `%USERPROFILE%\Dropbox\programs\pokemon-access\` | `%LOCALAPPDATA%\Temp\pokemon-access-gb\` |
| readers | `lua/` — 166 Lua + 8 DLLs | a partial copy |
| emulator | **`vba.exe`** (VisualBoyAdvance) | none |
| Lua runtime | **`lua51.dll`** (Lua 5.1 / LuaJIT) | n/a |
| version | **3.1.0** (`readme.txt`) | same scripts |

⛔ **`Temp/` is a poor place to work from** — Windows clears it, it is not backed up, and it
was missing pieces. The Dropbox tree is the source of truth and has been for years.

## What v3.1.0 supports

From `readme.txt`:

```
- Pokémon Red, Blue and Yellow.
- Pokémon Gold, Silver and Crystal.
- Pokémon Fire Red and Leaf Green.
- Pokémon Emerald.
```

Nine games across two generations — exactly the scope of the mGBA work.

## The native DLLs — and what each is for

This is the part that explains every FFI dependency I stubbed:

| DLL | purpose | needed under mGBA? |
|---|---|---|
| `lua51.dll` | the Lua 5.1 / LuaJIT runtime the readers are written for | **no** — mGBA supplies its own Lua |
| `Tolk.dll` + `nvdaControllerClient32.dll` + `SAAPI32.dll` | screen-reader output: Tolk, NVDA, System Access | **no** — OGA becomes the speech layer |
| `audio.dll` + `bass.dll` | sound cues (BASS audio library) | **no** — `audio.` is never called, verified by grep |
| `win-controls.dll` | native dialog boxes (InputBox, ComboBox) | **no** — interactive prompts only |
| `crc32.dll` | ROM checksums, via FFI | **already replaced** in pure Lua |

So the stub set is not a compromise — **every DLL is host UI or host audio, not reader
logic.** Replacing them is the correct layering, not a workaround: those responsibilities
belong to Open Game Access now.

## Confirmation that `audio.dll` really is unused

Earlier I bypassed `package.loadlib("audio.dll", ...)` on the grounds that `audio.` is never
referenced. The DLL's presence here confirms it is *loadable* — so the bypass is not hiding
a missing file, it is the right call. The module is loaded by the original boot and never
used by any of the 166 files.

## The crc32 difference is only line endings

The two `crc32.lua` files differ by MD5, and the difference is **CRLF vs LF** — same logic,
same interface. So the pure-Lua reimplementation in `oga_pure.lua` was written against the
correct source and does not need revisiting.

## Consequence for the mGBA work

Nothing already built is invalidated, and the plan is unchanged. Two adjustments:

1. **Point the bootstrap at the real tree.** `OGA_READER_DIR` should be
   `%USERPROFILE%\Dropbox\programs\pokemon-access\lua\`, not the Temp copy. The bootstrap
   already supports this via the environment variable, so no code change is needed.
2. **A VBA-vs-mGBA comparison is now possible**, and should eventually be done: v3.1.0 in
   VBA is the **known-good reference**. Running the same save in both and diffing the spoken
   output is the strongest available test that the mGBA port is faithful — much stronger
   than "it produced some text". This becomes card A6.

## Do not treat the DLLs as portable

The DLLs are 32-bit Windows binaries (note `nvdaControllerClient32.dll`) tied to VBA and to
this machine's screen readers. They are not part of the Open Game Access deliverable and
must never be committed — they are another party's redistributables. `.gitignore` already
covers `*.dll`; this note records *why* that matters here rather than assuming it.
