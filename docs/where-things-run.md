# Where everything runs — the Windows/WSL split

**Read this before touching anything.** The single most repeated source of
confusion in this project is doing an operation on the wrong side of the
Windows/WSL boundary: editing a file Windows cannot see, building with a tool
only one side has, or running a script that silently reads a stale copy.

## The rule

**Windows is the source of truth for the repository. WSL is where the builds
happen.** They are separate copies connected by `wsl.sh` (rsync), and the copies
drift if you skip the sync.

```
C:\Users\Devin Prater\open-game-access    <-- edit here (Windows)
/home/devin/open-game-access              <-- build here (WSL)
```

⛔ **Never edit the WSL copy directly.** `wsl.sh sync` (and `stage-all`) copy
Windows → WSL. An edit made on the WSL side is overwritten by the next sync
without warning.

⛔ **Never run a build from the Windows copy.** `xtool`, `clang++` for iOS, the
Darwin SDK and the melonDS source tree are all WSL-only.

## What lives where

| Thing | Side | Absolute path |
|---|---|---|
| Repo (edit here) | **Windows** | `C:\Users\Devin Prater\open-game-access` |
| Repo (build here) | **WSL** | `/home/devin/open-game-access` |
| `wsl.sh` dispatcher | **Windows** (runs WSL) | `<repo>/wsl.sh <script>` |
| Scripts | **Windows**, run in WSL | `<repo>/scripts/*.sh` |
| iOS core archive | **WSL** (build output) | `Vendor/libpokecore.a` |
| Simulator archive | **WSL** (build output) | `Vendor/sim/libpokecore-sim.a` |
| Host harness objects | **WSL** (build output) | `Vendor/hostobj/*.o` |
| Host probe binaries | **WSL** (build output) | `Vendor/fe_access`, `Vendor/dbz_probe`, ... |
| Swift package + app sources | **Windows** (edit) | `Sources/`, `Package.swift` |
| Built `.app` | **WSL** (build output) | `xtool/OpenGameAccess.app` |
| Built sim `.app` | **WSL** (build output) | `xtool-sim/OpenGameAccess.app` |
| melonDS source | **WSL** | `~/src/melonds-lua` (and `-irpatch`) |
| Lua 5.4 source | **WSL** | `~/src/lua-5.4.7` |
| Darwin SDK | **WSL** | `~/.swiftpm/swift-sdks/darwin.artifactbundle/...` |
| Swift toolchain | **WSL** | `/usr/local/swift` |
| xtool | **WSL** | `/usr/local/bin/xtool` → `/opt/xtool/xtool` |
| Android app (melonDS fork) | **Windows** | `%LOCALAPPDATA%\Temp\pokemon-a11y-app` |
| Android SDK / JDK 21 | **Windows** | `~/Android/Sdk`, `~/AppData/Local/Java/jdk-21` |
| Pokémon reader (user's copy) | **Windows** | `Dropbox\programs\pokemon-access\lua` |
| ROMs / saves (never in repo) | **Windows** | `Dropbox\Games`, `Dropbox\programs\pokemon-access\battery` |
| Ghidra project | **Windows** | `C:\Users\Devin Prater\oga-ghidra` |

## Tooling, by side

**Windows only:** Android/Gradle builds, JDK 21, Android SDK, adb/emulator,
Ghidra + PyGhidra, the Apple Devices app and the usbmux port-forward, scoop.

**WSL only:** Swift 6.3.3, xtool, the Darwin SDK, `clang++` cross-compiling to
arm64-apple-ios, `llvm-nm`/`llvm-ar`, the melonDS and Lua source trees,
rsync, `ideviceinfo`/`idevicesyslog`.

⛔ **`llvm-nm`, never GNU `nm`.** GNU nm cannot read Mach-O and prints nothing,
which reads as "the symbol is missing". Same for `llvm-ar` (GNU ar's index is not
readable by `ld64.lld`).

## The commands that actually work

```bash
# From Windows (git-bash). wsl.sh must run INSIDE WSL.
wsl.exe -d Ubuntu-24.04 -- bash -lc 'cd /home/devin/open-game-access && bash ./wsl.sh sync'
wsl.exe -d Ubuntu-24.04 -- bash -lc 'cd /home/devin/open-game-access && bash ./wsl.sh stage-all'
wsl.exe -d Ubuntu-24.04 -- bash -lc 'cd /home/devin/open-game-access && bash ./wsl.sh commit-push'
```

```bash
# iOS: build only
bash scripts/xtool-build.sh
# iOS: build + sign + install + launch on the attached phone
bash scripts/deploy.sh
# the emulator core archive (~4 min; only when core sources change)
bash scripts/build-core.sh
# the host object set for diagnostic harnesses
bash scripts/build-host.sh
```

### Two traps that have each cost a full build cycle

⛔ **`$VAR` does not survive nested quoting through `wsl.exe -- bash -lc '...'`.**

```bash
# BROKEN: $S expands to empty, every grep finds nothing, ls says "No such file"
wsl.exe ... -- bash -lc 'S=/path; ls "$S"'
```

Write the probe to a `.sh` file and run the file, or repeat the absolute path.
A false negative here looks like "the file/API does not exist".

⛔ **Never build a `wsl.exe -- bash -lc '...'` one-liner through the MSYS host
shell.** The parent shell expands `$PATH` and any `$VAR` inside your single
quotes before WSL sees it. Parentheses in the string are worse: they break the
whole command with a syntax error pointing at the wrong place. **Write a script
file.**

## Sync rules

`wsl.sh sync` excludes build outputs, deliberately:

```
--exclude '.build/'  --exclude 'xtool/'  --exclude 'xtool-sim/'
--exclude 'Vendor/obj/'  --exclude 'Vendor/*.a'  --exclude 'Vendor/hostobj/'
```

So a sync never clobbers a built archive, and never copies 64 MB of objects back
to Windows. To publish, use the allow-list staging instead (`stage-all`), which
copies only named sources into a clean tree — see
`references/publishing-allowlist-repos.md` for why that allow-list must be kept
in sync with new files.

## Verifying an install (iPhone)

`ideviceinstaller` is NOT installed on this machine, and
`ideviceinfo -q com.apple.mobile.installation_proxy` returns nothing useful. Use
the device's own log stream:

```bash
idevicesyslog -p OpenGameAccess > "$HOME/app.log" 2>/dev/null &
sleep 30; kill %1
```

- installed → the device shows `XTL-<TEAMID>.<bundle-id>`
- running → `AppName(<pid>)` appears in `audiomxd` lines
- **crash check: `grep -c 'EXC_'`, not a loose regex.** `grep -iE 'fault'` matches
  the `fault` in `default`, which appears in every scene-identity string.

⛔ **`/tmp` does not persist between separate `wsl.exe -- bash -l script.sh`
calls.** Write captures to `$HOME`, or capture and analyse in one invocation.

## ⛔ Three homes, one name — the trap that keeps recurring

`$HOME` means **different directories on each side**, and every tool that resolves
it will silently answer about its own side:

| Context | `$HOME` actually is |
|---|---|
| git-bash (MSYS) | `C:\Users\Devin Prater` |
| WSL (`bash -l`) | `/home/devin` |
| a native Windows program run from bash | `C:\Users\Devin Prater` |

Consequences that have each produced a **confident false result**:

⛔ **`$HOME/scoop` INSIDE WSL IS A DIFFERENT, EMPTY DIRECTORY — AND `mkdir -p` WILL
HAPPILY CREATE IT.** A core-download script wrote 7 emulator cores to
`/home/devin/scoop/persist/retroarch/cores`, printed `OK` for every one, and
reported a correct count — into a fake tree no emulator can see. The real install is
`/mnt/c/Users/Devin Prater/scoop`. **Scoop is Windows-only.**

⛔ **`$LOCALAPPDATA` IS UNSET IN WSL** and, under `set -u`, aborts the script with
`LOCALAPPDATA: unbound variable`. Use `/tmp` (carefully — see above) or a `/mnt/c`
path.

⛔ **A PRESENCE CHECK RUN ON THE WRONG SIDE REPORTS A FALSE `NO`.** A verifier run in
git-bash checked `$HOME/src/melonds-lua` and reported the melonDS source **missing** —
but it lives at `/home/devin/src/melonds-lua` **inside WSL**, present and building.
Ask the owning side explicitly:

```bash
# WRONG in git-bash: $HOME is the Windows home
[ -d "$HOME/src/melonds-lua" ] && echo yes

# RIGHT: ask WSL
wsl.exe -d Ubuntu-24.04 -- test -d /home/devin/src/melonds-lua && echo "yes (wsl)"
```

**The rule: verify a path from the side that owns it.** A check that guesses produces
false negatives, and a false negative reads as "the thing does not exist" — which is
indistinguishable from a real answer.

## ⛔ Native binaries need NATIVE paths

MSYS path conversion is disabled on this host, so a bash variable that naturally
produces `/c/Users/...` is **not** translated when handed to a native `.exe`:

```bash
# BROKEN — RetroArch exits 1 with an EMPTY log, which reads as "core refused"
-L /c/Users/Devin\ Prater/scoop/.../snes9x_libretro.dll

# RIGHT
-L "C:/Users/Devin Prater/scoop/persist/retroarch/cores/snes9x_libretro.dll"
```

This has bitten `git -C`, `rg`, `node`, and now RetroArch's `-L` and ROM arguments.
Prefer a native `C:/...` path whenever the argument reaches a Windows executable.

## ⛔ An emulator's own binary is not always `<package>.exe`

Resolve by glob rather than guessing, or a present emulator reads as absent:

```bash
DUCK="$(ls -1 "$HOME/scoop/apps/duckstation/current/"*duckstation-qt*.exe | head -1)"
SN="$(ls -1 "$HOME/scoop/apps/snes9x/current/"*snes9x*x64*.exe | head -1)"
MGBA="$(ls -1 "$HOME/scoop/apps/mgba-dev/current/"mGBA.exe | head -1)"
```

Real names found here: `duckstation-qt-x64-ReleaseLTCG.exe`, `snes9x-x64.exe`,
`mGBA.exe`, `pcsx2-qt.exe`, `PPSSPPWindows64.exe`, `Dolphin.exe`, `azahar.exe`.

## ⛔ Always wrap an emulator probe in `timeout`

A Qt GUI app given `--help` **blocks forever** waiting for a window.
`pcsx2-qt.exe --help` and `PPSSPPWindows64.exe --help` both hang and will consume the
entire command budget. `timeout 20 <exe> --help` is mandatory, not defensive.

## ⛔ A documented rule violated is worse than an undocumented one

The `/tmp` warning above existed before this session, and `/tmp/dbz_probe` still
vanished mid-work between two `wsl.exe -- bash -l` calls — turning a "run" into a
no-op whose only symptom was an empty log. **Probe binaries and RAM dumps now live
under `$HOME`** (`~/oga-probe`, `~/oga-ram.bin`). When a documented trap fires anyway,
the fix is to remove the possibility, not to remember harder.

