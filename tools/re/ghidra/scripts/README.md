# Ghidra headless query scripts (game-agnostic)

These scripts answer semantic questions about a program in a Ghidra database. They are
**deliberately game-agnostic**: nothing here knows what FE11 is, what a `Unit` is, or
where `Force.id` lives. Game knowledge belongs in `reverse-engineering/<game>/`, so the
same scripts work on a PSP MIPS binary or a PS2 Emotion Engine ELF without modification.

## ⛔ All scripts are prefixed `Oga` — do not remove the prefix

Ghidra resolves a script by **name across every script directory it knows**, and its own
bundled scripts come first. A file named `ListFunctions.java` is silently shadowed by
`Ghidra/Features/FunctionID/ghidra_scripts/ListFunctions.java`, so the headless run
executes Ghidra's version instead of ours. The symptom is not "script not found" — it is
Ghidra's script asking for an argument ours never had:

```
ERROR REPORT SCRIPT ERROR: java.lang.IllegalArgumentException:
  Error processing variable 'Output file Choose output file:'
  in headless mode -- it was not found in script arguments or a .properties file.
```

That reads as a bug in our script. It is a name collision. `SearchOffset.java` happens to
be safe because Ghidra ships no script by that name, but relying on that is luck — hence
the prefix on every script here.

The architectural rule:

```
Hermes asks a semantic question
        ↓
oga-re common interface      (same command for every game/platform)
        ↓
Ghidra/common script         (these files)
        ↓
platform adapter if needed   (tools/re/platforms/<platform>/)
        ↓
game analysis database       (Ghidra project)
```

A question like *"show every function that reads offset +0x08 from Force"* must not
require the caller to know whether the target is DS ARM, PSP MIPS, or PS2 Emotion
Engine. That is the whole point of this layer.

## Scripts

| Script | Question it answers |
|---|---|
| `OgaListFunctions.java` | what functions exist, with addresses and sizes |
| `SearchOffset.java` | which functions touch a structure offset (e.g. `+0x08`) |
| `OgaXrefsTo.java` | what references this address or symbol, and how |
| `OgaCallers.java` | who calls this function |
| `OgaCallees.java` | what this function calls |
| `OgaSearchConstant.java` | where a constant value appears in code |
| `OgaDecompile.java` | decompiler output for a function |
| `OgaExportSymbols.java` | all symbols as JSON, for interchange with other tools |
| `OgaListMemoryBlocks.java` | memory blocks / modules / overlays and their address ranges |
| `OgaStructureAccess.java` | reads vs writes of each field of a structure |

## Conventions

**Output is machine-readable.** Scripts print JSON (or a stable TSV) on stdout so the
result can be consumed programmatically rather than read by eye. A `print` of prose is
a bug in a query script.

**Confidence is never invented here.** A script reports what the database contains. If a
function is `FUN_02042a10`, that is what it returns — naming it `GetUnitAtPosition` is a
judgement that belongs in the per-game findings with evidence, not in a query tool.

**No auto-analysis on query.** Queries run with `-noanalysis`: analysis is a deliberate
step, not a side effect of asking a question. Re-running analysis on every query would
be slow and could change results between questions.

## Running them on Windows (important)

Ghidra's launcher is a native Windows program reached through `analyzeHeadless.bat`, and
paths with spaces break when passed through `bash -> cmd.exe`. Use the `.bat` wrappers in
`ghidra/headless/`, which hold the arguments where Windows quoting applies and nothing
else can reinterpret them. Two failures this avoids, both of which read as bugs in
Ghidra rather than in the invocation:

- `ClassNotFoundException: LaunchSupport` — java received an MSYS-style `/c/Users/...`
  path it cannot resolve.
- `InvalidInputException: Bad argument: <projectname>` — the project path was split at
  the space, so Ghidra saw a truncated directory and blamed the project name.
