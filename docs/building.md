# Building Open Game Access

Three build paths: the iOS app, the Android app, and the host-side tools used for
reverse engineering. Everything is reproducible from a clean checkout **plus your
own copy of the game you want to play**.

## 0. Prerequisites

| Need | For |
|---|---|
| Linux or WSL2 (Ubuntu 24.04 recommended) | iOS build, host tools |
| Swift 6.3+ | iOS build |
| xtool 1.19+ | iOS build/sign/install without Xcode |
| Apple Developer account | iOS: the Darwin SDK; and a certificate for device installs |
| JDK 21 + Android SDK (platform 34) | Android build |
| `g++`, `git`, `curl` | host tools, dep fetch |

## 1. Get the dependencies

```bash
./scripts/bootstrap-deps.sh        # fetches melonDS-lua + Lua 5.4 into ~/src
```

The emulator core is **not vendored** in this repository — it is a separate
GPL-3.0 project, fetched at a pinned revision so published builds stay
reproducible. See the README's third-party section.

## 2. iOS

### Apple SDK (required, not distributable)

The iOS build needs Apple's Darwin SDK. It is Apple-licensed and cannot be shipped
here. Extract it from Xcode with your own Apple ID:

```bash
xtool auth login          # your Apple ID
xtool sdk install <Xcode_*.xip>
```

Then:

```bash
./wsl.sh build-core       # core archive for arm64-apple-ios17.0
./wsl.sh verify-device    # builds the device .app and checks it is real:
                          #   - LC_BUILD_VERSION platform ios
                          #   - melonDS + poke_* symbols present
                          #   - bundled Lua hashes match the originals
```

### iOS Simulator app (no signing needed)

```bash
./wsl.sh build-sim-app    # -> xtool-sim/OpenGameAccess.app + OpenGameAccess-simulator.zip
```

`xtool dev build --triple arm64-apple-ios-simulator` compiles and links the
simulator binary but only writes an `.app` for the *device* triple, so
`scripts/package-sim-app.sh` assembles the bundle from the linked binary plus the
SwiftPM resource bundle, with `CFBundleSupportedPlatforms = [iPhoneSimulator]` and
`DTPlatformName = iphonesimulator`.

On a Mac:

```bash
xcrun simctl install booted OpenGameAccess.app
xcrun simctl launch booted com.devinprater.opengameaccess
```

### iOS device install (your own certificate)

Signing an `.ipa` requires a certificate and provisioning profile tied to your
Apple account, so no generic download can install on your phone. Build it yourself:

```bash
./wsl.sh deploy           # mirror, build core, build+sign+install+launch
```

`deploy` needs the phone visible to the build machine. On Windows/WSL the working
method is forwarding Apple's Windows mobile-device mux into WSL — **`usbipd-win`
does not work for iOS bulk transfers** (usbipd-win#959, still open). See
[`docs/device-access-wsl.md`](device-access-wsl.md).

## 3. Android

```bash
cd app
export JAVA_HOME=/path/to/jdk-21
export ANDROID_HOME=$HOME/Android/Sdk
./gradlew :app:assembleGitHubProdDebug
# -> app/build/outputs/apk/gitHubProd/debug/app-gitHub-prod-debug.apk
```

The Android app is a fork of **melonDS-android** (rafaelvcaetano) with the
accessibility layer added; `app/native-overlay/` holds the files that override the
upstream clone (the Lua script host, the mGBA runner, the accessibility Kotlin).
Set up the clone, then apply the overlay:

```bash
git clone https://github.com/rafaelvcaetano/melonDS-android
cd melonDS-android
# swap the melonDS submodule for the Lua fork, then copy app/native-overlay/* over
```

Note: reinstalling the Android app wipes its SAF ROM directory, so you will be
asked to pick your ROM folder again after each install.

## 4. Host tools (reverse engineering)

These run the same core on the development machine and are how every memory
address in `docs/` was found. They are the part most worth reusing for a new game.

```bash
./wsl.sh build-host        # objects for the host (x86_64 Linux)
./wsl.sh fe-access units 5000   # run the Fire Emblem adapter, print state
./wsl.sh fe-run units 5000      # raw structure dump
./wsl.sh fe-probe               # scripted plan + RAM snapshots + screenshots
```

| Tool | Purpose |
|---|---|
| `Core/probe.cpp` | scripted input plan → paired RAM snapshots + screenshots |
| `Core/ramscan.cpp` | is a claimed address region populated at all? |
| `Core/ramwatch.cpp` | watch addresses across a run; first/last/changed |
| `scripts/ramdiff.py` | rank changed addresses by *signal shape* |
| `scripts/cursorstruct.py` | dump a whole struct across snapshots |
| `scripts/ppm2png.py` | screenshots you can actually look at |

**Method rule that saved the most time:** check for a decompilation or disassembly
before writing a scanner. For Fire Emblem: Shadow Dragon, `Eebit/fe11-us` provides
named symbols with real addresses, which turned "find the cursor by diffing RAM"
into "read `gMapStateManager + 0x10 → Cursor + 0x08`".

## 5. Your ROMs

Not included and never will be. Supply your own. Tested with Pokémon Black/White
(`IRBO`/`IRAO`) and Fire Emblem: Shadow Dragon USA (`YFEE`).

Put them where the scripts look, or pass a path:

```bash
mkdir -p ~/roms ~/hosttest-data
cp /path/to/your/game.nds ~/roms/
```

## Troubleshooting

**`swift build` fails with `could not find CLI tool 'libtool'`** — install
`libtool-bin`; the `libtool` package only ships `libtoolize`.

**`ld64.lld: error: archive has no index`** — the archive was made with GNU `ar`.
Use the Swift toolchain's own: `/usr/local/swift/bin/llvm-ar rcs` +
`llvm-ranlib`. The build scripts already do.

**`xtool devices` hangs forever** — `/var/run/usbmuxd` is missing. Wrap it in
`timeout`, and check the mux socket first.

**Link fails with `undefined symbol: melonDS::Platform::…`** — the core grew a
Platform hook the shim does not define. The link errors name the symbols; add
matching stubs to `Core/poke_platform.cpp`.
