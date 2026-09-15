# oga-re checkpoint — toolchain and FE11 status

Machine: Windows 11 · Shell: git-bash (MSYS) · Recorded 2026-09-14

This is the deliverable checkpoint requested after standing up the reverse-engineering
environment. Every claim below was produced by a command whose output is quoted. Where
something is NOT verified, it says so.

---

## 1. Installed tool versions (verified)

| Tool | Version | Source |
|---|---|---|
| Java (Ghidra's JDK) | **Temurin 21.0.12.1+1 LTS** | `java -version` under `JAVA_HOME` |
| Ghidra | **12.1.3** (PUBLIC, build 20260817) | `application.properties` |
| PyGhidra | **3.1.0** | `python -c "import pyghidra"` |
| Python (user's) | **3.14.7** | scoop `apps/python/3.14.7` |
| Python (PyGhidra host) | **3.13.15** | see the incompatibility below |
| git | 2.55.0.5 (2.55.0.windows.5) | `git --version` |
| 7zip | 26.03 | scoop manifest |
| ninja | 1.13.2 | `ninja --version` |
| cmake | 4.4.3 | `cmake --version` |
| make | 4.4.1 | `make --version` |
| melonDS (Lua fork) | vendored source at `~/src/melonds-lua` | builds 113 TUs |

### Tools deliberately NOT installed

- **rust / cargo** — `dsd` ships a prebuilt `dsd-windows-x86_64.exe`, so no Rust toolchain
  is needed for it. Rust is only required to build dsd from source (needs Rust >= 1.85,
  edition 2024) or to build dsd-ghidra's native library. Install `rustup-msvc` rather than
  the standalone `rust` package if a rustup-managed toolchain is ever wanted — the
  standalone package needs MSVC Build Tools to link.

---

## 2. Two real environment problems, found and fixed

### ⛔ `JAVA_HOME` pointed at JDK 17 while Ghidra needs JDK 21

The machine had `JAVA_HOME=C:\Users\Devin Prater\AppData\Local\Java\jdk-17.0.20.1+1`.
Ghidra's launcher reads **`JAVA_HOME` and ignores the PATH**, so having JDK 21 installed
is not enough. Ghidra failed with:

```
WARNING: JAVA_HOME environment specifies unsupported java version: ...jdk-17.0.20.1+1
JDK 21+ (64-bit) could not be found and must be manually chosen!
ERROR: Failed to find a supported JDK.
```

Fixed with `setx JAVA_HOME <temurin21>`. **Caveat that cost time:** `setx` only affects
processes started *afterwards* — an existing shell keeps the stale value, so the error
persists in that shell even after the fix looks applied. Tooling here exports `JAVA_HOME`
explicitly per invocation rather than trusting the environment.

### ⛔ PyGhidra 3.1.0 cannot install on Python 3.14

PyGhidra 3.1.0 pins `Jpype1==1.5.2`, and JPype1 has no `cp314` wheel — only 1.7.x
publish one. `pip install` therefore fell back to building JPype1 from source, which fails
without MSVC:

```
ERROR: Failed building wheel for Jpype1
error: failed-wheel-build-for-install
```

This is the "specific tool demonstrably fails" case, so Python 3.14 is **not** downgraded
(it stays the user's default). PyGhidra is instead installed into **Python 3.13.15**,
where a `jpype1-1.5.2-cp313` wheel exists:

```
Successfully installed Jpype1-1.5.2 packaging-26.3 pyghidra-3.1.0
pyghidra OK 3.1.0
```

Worth noting Ghidra's own docs claim PyGhidra supports Python 3.9-3.14; the pin on
JPype1 makes 3.14 fail in practice. Documentation is not evidence — the wheel list is.

---

## 3. Ghidra launches successfully — verified

`analyzeHeadless` created a project, loaded a program, and saved it:

```
INFO  Creating project: C:\Users\Devin Prater\oga-ghidra\proj\OgaSmoke (DefaultProject)
INFO  REPORT: Import succeeded (HeadlessAnalyzer)
```

### ⛔ Two invocation traps that both read as Ghidra bugs

**Trap 1 — MSYS paths to a native Windows program.** Calling
`support/analyzeHeadless` from git-bash hands `java` a path like `/c/Users/...`, which it
cannot resolve:

```
Error: Could not find or load main class LaunchSupport
Caused by: java.lang.ClassNotFoundException: LaunchSupport
```

This looks like a broken install. It is a path-format problem: MSYS path conversion is
disabled on this host, so java receives the path untranslated. Use the `.bat` launcher.

**Trap 2 — the space in `Devin Prater` splits the project path.** Passing a Windows path
with a space through `bash -> cmd.exe /c "analyzeHeadless.bat C:\Users\Devin Prater\proj
Name"` truncates it at the space, and Ghidra blames the wrong thing:

```
ghidra.util.exception.InvalidInputException: Bad argument: Name
```

The argument named in the error is the project *name*, but the real failure is that
`project_location` became `C:\Users\Devin`. **Fix: put the arguments in a `.bat` file**,
where Windows quoting applies and nothing else reinterprets them.

---

## 4. PyGhidra / headless status

- `analyzeHeadless` — **works** (project create, import, analyse, save all verified).
- `pyghidraRun` — present in `support/`.
- PyGhidra 3.1.0 — **imports and runs** on Python 3.13.15.
- ⛔ Ghidra 12.x changed the default Python engine from Jython to **PyGhidra**, and 12.1
  demoted Jython to an optional extension, so Python scripts must target PyGhidra. An
  older Jython script needs an explicit `# @runtime Jython` header.

---

## 5. Ghidra-version compatibility conflicts (the important finding)

| Component | Supported Ghidra |
|---|---|
| dsd-ghidra v0.7.0 | **11.2.1 only** |
| NTRGhidra v1.5.1 | 12.0.4 |
| NTRGhidra v1.4.4.1-test3 | 11.3.1 |
| PyGhidra 2.x | 11.3+ |
| PyGhidra 3.x | 12.0+ |
| dsd (Rust CLI) | none — version-independent |

**No single Ghidra install satisfies dsd-ghidra and NTRGhidra together**, and Ghidra
11.2.1 predates PyGhidra entirely, so symbol sync on 11.2.1 cannot be headless.

Mismatched extensions *do* install (red highlight + confirm prompt), but the 11→12 jump
upgraded internal jars and pre-12 extension zips have failed outright with
`NoSuchMethodError` in `ExtensionUtils.createExtensionDetailsFromArchive`. Project data
also does not go backwards: a project touched in 12.x cannot be reopened in 11.2.1.

**Resolution: two side-by-side Ghidra installs.** Scoop provides only 12.1.3, so 11.2.1
must be fetched manually from GitHub releases. 12.1.3 is installed now and used for
everything below; 11.2.1 is required before dsd-ghidra symbol sync is attempted.

⛔ **dsd-ghidra's symbol sync is GUI-only.** `SyncDsd.java` is a plain `GhidraScript`, it
unconditionally builds a Swing `DsdConfigChooser` file dialog and calls
`getSelectedFile()`, and a scan of every `.java` file for `isRunningHeadless`,
`HeadlessAnalyzer` and `HeadlessScript` finds **zero** hits with no `getScriptArgs()`
handling. Automating it needs a wrapper that bypasses the dialog and calls the
`dsdghidra.sync.*` classes directly. This is a real limitation for an agent-driven
workflow and is not yet solved.

---

## 6. `dsd` status — NOT YET INSTALLED

`dsd` is required for `dsd rom extract` and for generating the delinks/symbols config that
dsd-ghidra consumes. Not yet downloaded. Its documented Windows path is the prebuilt
release binary `dsd-windows-x86_64.exe` (v0.12.1, 2026-09-07) — no Rust toolchain needed.
`dsd --help` has therefore not been run and its commands are unverified **on this machine**,
though their purposes are documented in the toolchain notes.

---

## 7. FE11 ROM identity and layout

| | |
|---|---|
| ROM | Fire Emblem: Shadow Dragon (USA), user-supplied |
| Game code | `YFEE` · revision 0 · title `FIREEMBLEM11` |
| SHA-256 | `bebe9ce0d727a61f1676dd8360e2d4fe7be936b9123e187f106c6cbd555714d7` |
| **Matches fe11-us's required SHA-1** | **yes** — `7b7b307e…c1765e` |

### ARM9 / overlay layout — extracted from the ROM

```
ARM9  rom=0x00004000  entry=0x02000800  ram=0x02000000  size=952952  (ends 0x020E8A78)
ARM7  rom=0x00208200  entry=0x02380000  ram=0x02380000  size=159528
OVL   table=0x000ECC00  size=384  -> 12 entries
```

**12 overlays, matching fe11-us's ov000–ov011 exactly** — independent confirmation that
our ROM is the revision the decompilation targets. Extracted with
`tools/re/platforms/nds/extract-nds-arm9.py` (reads the user's own ROM, refuses to write
inside the repository, output never committed).

⛔ **Overlay binary extraction has a bug**: `overlay_table.tsv` shows implausible file
ids (e.g. `35501712`), so the per-entry field offsets in that first parser are wrong and
0 overlay binaries were written. The ROM header values above are correct; the overlay
*decomposition* should be redone with `dsd rom extract` once dsd is installed rather than
debugging a hand-rolled parser for a job the official tool already does.

---

## 8. Ghidra project built — and what it contains

Imported `arm9.bin` with `-loader BinaryLoader -loader-baseAddr 0x02000000 -processor
ARM:LE:32:v5t`, full auto-analysis:

```
ARM Constant Reference Analyzer      7.818 secs
Create Function                      1.034 secs
Decompiler Switch Analysis          39.261 secs
Function Start Search                0.459 secs
Function Start Search After Code     1.263 secs
INFO  REPORT: Analysis succeeded for file: .../arm9.bin
INFO  REPORT: Save succeeded for: Fe11Arm9:/arm9.bin
```

A query through the oga-re layer returns:

```json
"program": "arm9.bin",
"language": "ARM:LE:32:v5t",
"totalFunctions": 3774,
"autoNamedFunctions": 3774,
"namedFunctions": 0
```

⛔ **`namedFunctions: 0` is the honest headline.** A stock Ghidra import knows *nothing*
about this binary — 3,774 functions, every one named `FUN_xxxxxxxx`. This is precisely
what dsd-ghidra symbol sync exists to fix, and why the two-Ghidra split matters: the
project with real names requires Ghidra 11.2.1, which is not installed yet.

---

## 9. `Force.id` (runtime correlation target)

| | |
|---|---|
| Field | `Force.id` at **`+0x08`** |
| Width | `s32` |
| Values seen | 0 player, 1 enemy, 2 player(scenario), 3 enemy(scenario), 4 unassigned reserve (≈60 roster slots), 5 other |
| Evidence | decomp `include/unit.hpp` + `src/force.cpp`; live read: all six `gForces` entries have `id == index` |
| Enum exists in decomp? | **No** — no named constants anywhere; only literals (`new Force[6]`, `gForces[4]`, `unit->force->id == 4`, `== 5`) |

Static correlation (Phase 16) has **not** been done yet: it requires the symbol-synced
Ghidra project, because searching an unnamed 3,774-function database for "reads +0x08"
returns noise without named structure types to anchor it.

---

## 10. Next structures/functions for accessibility

Ranked by what Open Game Access must report next. Each is a semantic goal, not yet a
located function.

| # | Goal | Why |
|---|---|---|
| 1 | `GetUnitAt(x, y)` | occupancy lookup; underlies "unit here" and targeting |
| 2 | Movement-range generation | already computed from cost matrix; the *game's* function would confirm it |
| 3 | Weapon range / target filtering | needed for attackable-unit reporting |
| 4 | `MapStateManager` camera / bounds | map extents and viewport, for spatial phrasing |
| 5 | Chapter / objective state | "seize the gate" style objectives |
| 6 | Battle forecast struct | damage/hit/crit before committing |
| 7 | Menu / command state | which menu is open, which command is highlighted |
| 8 | Unit status effects | beyond HP: poison, sleep, etc. |
| 9 | Equipped weapon / inventory | currently only `items[5]` known |
| 10 | Turn counter / phase | whose turn it is |

### Not investigated at all
Text/dialogue (RAM holds message IDs but no prose; plain ASCII scan of the ROM found
none), current-chapter identity, unit `state1` bit meanings.

---

## 11. Scripts and files added

| File | Purpose |
|---|---|
| `scripts/fe-identity.sh` | ROM identity record (hashes, header fields) for any platform |
| `tools/re/platforms/nds/extract-nds-arm9.py` | ARM9/ARM7/overlay extraction from the user's ROM |
| `tools/re/ghidra/headless/ghidra-run.sh` | install check + launcher with correct `JAVA_HOME` |
| `tools/re/ghidra/headless/ghidra-headless.sh` | headless wrapper documenting both invocation traps |
| `tools/re/ghidra/scripts/OgaListFunctions.java` | enumerate functions as JSON with an `autoNamed` flag |
| `tools/re/ghidra/scripts/OgaSearchOffset.java` | find every function accessing a struct offset |
| `tools/re/common/check-toolchain.py` | report installed versions from scoop manifests |
| `tools/re/manifests/tools.json` | exact versions + the Ghidra compatibility matrix |
| `tools/re/manifests/platforms.json` | per-platform capability matrix |
| `tools/re/common/schemas/address.json` | the common address model (platform/space/bank/module) |
| `tools/re/common/schemas/findings.json` | shared schema for per-game semantic findings |
| `docs/reverse-engineering/fe11.md` | durable FE11 notes with confidence levels |
| `reverse-engineering/fe11/symbols.json` | machine-readable FE11 findings |
| `.gitignore` | extended to every target platform's ROM/disc/savestate extensions |

---

## 12. Known traps, recorded so they cost no time twice

1. **`JAVA_HOME` beats `PATH`** for Ghidra, and `setx` does not affect running shells.
2. **MSYS paths break native Windows programs** — use the `.bat` launcher.
3. **Spaces in `C:\Users\Devin Prater` split arguments** through `bash -> cmd.exe`; put
   them in a `.bat` file.
4. **Prefix every Ghidra script `Oga`** — Ghidra's own `ListFunctions.java` silently
   shadows ours, and the symptom is Ghidra's script complaining about an argument ours
   never had.
5. **PyGhidra 3.1.0 needs Python <= 3.13** (Jpype1 pin), despite docs claiming 3.14.
6. **Ghidra 12.x defaults to PyGhidra**; Jython is now an optional extension.

---

## 13. Platform tooling status

| Platform | Status |
|---|---|
| Nintendo DS | **partially ready** — ARM9 imported and queryable; dsd and dsd-ghidra not yet installed; dsd-ghidra needs a second Ghidra (11.2.1) |
| PSP, GBA, GB/GBC, NES, SNES, Genesis, N64, PS1, PS2 | **research needed** — see `platforms.json` |

No other platform has been claimed as supported. Per the brief, nothing is marked ready
until it has been tested.
