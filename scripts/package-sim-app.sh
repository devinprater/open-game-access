#!/usr/bin/env bash
# package-sim-app.sh — assemble a real iOS-Simulator .app bundle.
#
# Two ways in:
#   1. A binary already linked by `xtool dev build --triple …-simulator` (Linux/WSL
#      path; xtool links but only packages the DEVICE triple, so the bundle is
#      assembled here from the linked binary + resources).
#   2. No pre-linked binary (e.g. a macOS CI runner, which has Xcode but not
#      xtool): link it here with swift build against the simulator triple.
#
# ⛔ The second path exists because CI failed with "no simulator binary at
# .build/... — run build-sim-app.sh first". The core had built fine; only the
# Swift link step was missing, because that step was assumed to have happened
# elsewhere. This script now does it when it has not.
set -uo pipefail
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
APP="$ROOT/xtool-sim/PokemonAccess.app"
BIN="$ROOT/.build/arm64-apple-ios-simulator/debug/PokemonAccess-App"
BUNDLE_ID="com.devinprater.pokemonaccess"
TRIPLE="${TRIPLE:-arm64-apple-ios-simulator}"

if [ ! -f "$BIN" ]; then
  echo "== no pre-linked simulator binary; building it here with swift build"
  SIMLIB="$ROOT/Vendor/sim/libpokecore-sim.a"
  [ -f "$SIMLIB" ] || { echo "!! simulator core archive missing: $SIMLIB" >&2; exit 1; }
  cd "$ROOT" || exit 1
  export POKECORE_LIB="$SIMLIB"

  # ⛔ swift build MUST be told which SDK to use. Without it, SwiftPM resolves the
  # standard library against the macOS sysroot and fails with
  #   warning: using sysroot for 'MacOSX' but targeting 'iPhone'
  #   error: unable to load standard library for target 'arm64-apple-ios17.0-simulator'
  # — one error per source file, which reads like a broken toolchain but is just an
  # unset SDK.
  SIMSDK="${SDKROOT:-}"
  if [ -z "$SIMSDK" ] && command -v xcrun >/dev/null 2>&1; then
    SIMSDK="$(xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null || true)"
  fi
  if [ -z "$SIMSDK" ] && [ -d "$HOME/.swiftpm/swift-sdks/darwin.artifactbundle" ]; then
    SIMSDK="$(find "$HOME/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs" -maxdepth 1 -name 'iPhoneSimulator*.sdk' 2>/dev/null | head -1)"
  fi
  [ -d "${SIMSDK:-}" ] || { echo "!! no iPhoneSimulator SDK found (set SDKROOT)" >&2; exit 1; }
  echo "   SDK: $SIMSDK"

  # SwiftPM takes a target triple directly; this is the same link xtool performs,
  # minus xtool's app-bundle packaging (which this script does itself).
  swift build --triple "$TRIPLE" --sdk "$SIMSDK" -c debug 2>&1 | tail -20
  if [ ! -f "$BIN" ]; then
    # swift build may place the product under a different leaf; find it.
    FOUND=$(find "$ROOT/.build" -name 'PokemonAccess-App' -type f 2>/dev/null | head -1)
    [ -n "$FOUND" ] && BIN="$FOUND"
  fi
fi

[ -f "$BIN" ] || { echo "!! could not produce a simulator binary (looked at $BIN)" >&2; exit 1; }

echo
echo "== binary =="
file "$BIN"
PLAT=$("$(command -v llvm-objdump || echo /usr/bin/otool)" --macho --private-headers "$BIN" 2>/dev/null \
       | grep -A4 'LC_BUILD_VERSION' | grep -m1 platform)
[ -z "$PLAT" ] && PLAT=$(otool -l "$BIN" 2>/dev/null | grep -A4 'LC_BUILD_VERSION' | grep -m1 platform)
echo "LC_BUILD_VERSION: ${PLAT:-<not found>}"
case "$PLAT" in
  *iossimulator*) echo "-> iOS Simulator platform (correct)" ;;
  *"platform ios"*) echo "!! device platform — the linker picked the DEVICE target" ;;
  *) echo "!! could not confirm the platform from the load commands" ;;
esac

rm -rf "$APP"
mkdir -p "$APP"
cp "$BIN" "$APP/PokemonAccess"

# The SwiftPM resource bundle travels with the app: it holds the Lua script, so an
# app assembled without it installs and then finds no script.
RES=""
for cand in "$ROOT/.build/arm64-apple-ios-simulator/debug/PokemonAccess_PokemonAccess.bundle" \
            "$ROOT/.build"/*/debug/PokemonAccess_PokemonAccess.bundle; do
  [ -d "$cand" ] && { RES="$cand"; break; }
done
if [ -n "$RES" ]; then
  cp -R "$RES" "$APP/"
  echo "== resources: $(find "$APP/PokemonAccess_PokemonAccess.bundle" -type f | wc -l) files"
  ls "$APP/PokemonAccess_PokemonAccess.bundle/Resources/" 2>/dev/null
else
  echo "!! no resource bundle — the Lua script would be missing from the app" >&2
  exit 1
fi

cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>en</string>
	<key>CFBundleDisplayName</key><string>Open Game Access</string>
	<key>CFBundleExecutable</key><string>PokemonAccess</string>
	<key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>PokemonAccess</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>CFBundleVersion</key><string>1</string>
	<!-- The SIMULATOR platform, not iPhoneOS: this is the pair of keys a
	     simulator runtime validates before it will launch the app. -->
	<key>CFBundleSupportedPlatforms</key><array><string>iPhoneSimulator</string></array>
	<key>DTPlatformName</key><string>iphonesimulator</string>
	<key>MinimumOSVersion</key><string>17.0</string>
	<key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
	<key>UILaunchScreen</key><dict/>
	<key>UIRequiredDeviceCapabilities</key><array><string>arm64</string></array>
	<key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationPortrait</string></array>
	<key>UISupportedInterfaceOrientations~ipad</key>
	<array>
		<string>UIInterfaceOrientationPortrait</string>
		<string>UIInterfaceOrientationPortraitUpsideDown</string>
		<string>UIInterfaceOrientationLandscapeLeft</string>
		<string>UIInterfaceOrientationLandscapeRight</string>
	</array>
</dict>
</plist>
PLIST

# Optional ad-hoc signature; a simulator does not enforce it, but signing makes the
# bundle acceptable to stricter installers.
/usr/local/swift/bin/ldid -S "$APP/PokemonAccess" 2>/dev/null || \
  command -v ldid >/dev/null 2>&1 && ldid -S "$APP/PokemonAccess" 2>/dev/null || \
  echo "(unsigned — fine for a simulator)"

echo
echo "== $APP"
ls -la "$APP"

# ⛔ Plain llvm-nm, no -g/-gU. GNU nm cannot read Mach-O at all, and llvm-nm -g
# on this binary prints a single line — both reported "0 melonDS symbols / 0 poke_"
# for a perfectly linked app, which sent me hunting a link bug that did not exist.
# scripts/verify-sim-app.sh cross-checks this against the total symbol count, so a
# tooling failure can no longer masquerade as a broken build.
NM_BIN=""
for c in /usr/local/swift/bin/llvm-nm llvm-nm /usr/bin/llvm-nm; do
  { [ -x "$c" ] || command -v "$c" >/dev/null 2>&1; } && { NM_BIN="$c"; break; }
done
[ -z "$NM_BIN" ] && NM_BIN="$(command -v nm || echo nm)"

echo "--- total symbols ---"
"$NM_BIN" "$APP/PokemonAccess" 2>/dev/null | wc -l
echo "--- core linked in? (melonDS symbols) ---"
"$NM_BIN" "$APP/PokemonAccess" 2>/dev/null | grep -ci melonds || true
echo "--- poke_ entry points ---"
"$NM_BIN" "$APP/PokemonAccess" 2>/dev/null | grep -c 'poke_' || true
echo "--- script hashes (must equal the originals) ---"
shasum -a 256 "$APP/PokemonAccess_PokemonAccess.bundle/Resources/"*.lua 2>/dev/null || \
  sha256sum "$APP/PokemonAccess_PokemonAccess.bundle/Resources/"*.lua 2>/dev/null

echo "--- zip for a simulator service ---"
cd "$ROOT/xtool-sim"
rm -f PokemonAccess-simulator.zip
zip -qr PokemonAccess-simulator.zip PokemonAccess.app
ls -la PokemonAccess-simulator.zip
