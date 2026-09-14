#!/usr/bin/env bash
# package-sim-app.sh — assemble a real iOS-Simulator .app bundle.
#
# `xtool dev build --triple arm64-apple-ios-simulator` compiles and links the
# simulator binary, but its packaging step only writes the .app for the DEVICE
# triple, so the bundle is assembled here from the linked binary plus the
# generated Info.plist. The result is a normal simulator app: install it with
#
#   xcrun simctl install booted PokemonAccess.app
#   xcrun simctl launch booted com.devinprater.pokemonaccess
#
# on a macOS host (or upload the zip to a cloud simulator service).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/.build/arm64-apple-ios-simulator/debug/PokemonAccess-App"
APP="$ROOT/xtool-sim/PokemonAccess.app"
BUNDLE_ID="com.devinprater.pokemonaccess"

[ -f "$BIN" ] || { echo "!! no simulator binary at $BIN — run build-sim-app.sh first" >&2; exit 1; }

echo "== binary =="
file "$BIN"
echo "-- platform load command --"
# -platform_version <platform> <minos> <sdk>: platform 2 is iOS, 7 is iOS
# Simulator. This is what a simulator runtime checks before launching.
PLAT=$(/usr/local/swift/bin/llvm-objdump --macho --private-headers "$BIN" 2>/dev/null \
       | grep -A4 'LC_BUILD_VERSION' | grep -m1 platform)
echo "LC_BUILD_VERSION: ${PLAT:-<not found>}"
case "$PLAT" in
  *iossimulator*|*7*) echo "-> iOS Simulator platform (correct)" ;;
  *platform\ ios*)    echo "!! device platform — the linker picked the DEVICE target" ;;
  *)                  echo "!! could not confirm the platform from the load commands" ;;
esac

rm -rf "$APP"
mkdir -p "$APP"

# The SwiftPM resource bundle travels with the app: it is where the Lua script
# lives, so an app assembled without it installs and then finds no script.
RES="$ROOT/.build/arm64-apple-ios-simulator/debug/PokemonAccess_PokemonAccess.bundle"
cp "$BIN" "$APP/PokemonAccess"
if [ -d "$RES" ]; then
  cp -R "$RES" "$APP/"
  echo "== resources: $(find "$APP/PokemonAccess_PokemonAccess.bundle" -type f | wc -l) files"
  ls "$APP/PokemonAccess_PokemonAccess.bundle/Resources/" 2>/dev/null
else
  echo "!! no resource bundle next to the binary — the Lua script would be missing" >&2
fi

cat > "$APP/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleDisplayName</key>
	<string>Pokemon Access</string>
	<key>CFBundleExecutable</key>
	<string>PokemonAccess</string>
	<key>CFBundleIdentifier</key>
	<string>$BUNDLE_ID</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>PokemonAccess</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0.0</string>
	<key>CFBundleVersion</key>
	<string>1</string>
	<!-- The SIMULATOR platform, not iPhoneOS: this is the pair of keys a
	     simulator runtime validates before it will launch the app. -->
	<key>CFBundleSupportedPlatforms</key>
	<array>
		<string>iPhoneSimulator</string>
	</array>
	<key>DTPlatformName</key>
	<string>iphonesimulator</string>
	<key>MinimumOSVersion</key>
	<string>17.0</string>
	<key>UIDeviceFamily</key>
	<array>
		<integer>1</integer>
		<integer>2</integer>
	</array>
	<key>UILaunchScreen</key>
	<dict/>
	<key>UIRequiredDeviceCapabilities</key>
	<array>
		<string>arm64</string>
	</array>
	<key>UISupportedInterfaceOrientations</key>
	<array>
		<string>UIInterfaceOrientationPortrait</string>
	</array>
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

# Optional ad-hoc signature; a simulator does not enforce it, but signing makes
# the bundle acceptable to stricter installers and costs a second.
/usr/local/swift/bin/ldid -S "$APP/PokemonAccess" 2>/dev/null || \
  echo "(unsigned — fine for a simulator)"

echo
echo "== $APP"
ls -la "$APP"
echo "--- core linked in? (melonDS symbols) ---"
/usr/local/swift/bin/llvm-nm "$APP/PokemonAccess" 2>/dev/null | grep -c melonDS
echo "--- poke_ entry points ---"
/usr/local/swift/bin/llvm-nm -g "$APP/PokemonAccess" 2>/dev/null | grep -c ' T _poke_'
echo "--- script hashes (must equal the originals) ---"
sha256sum "$APP/PokemonAccess_PokemonAccess.bundle/Resources/bizhawk_compat.lua" \
          "$APP/PokemonAccess_PokemonAccess.bundle/Resources/main.lua" 2>/dev/null
echo "--- zip for a simulator service ---"
( cd "$ROOT/xtool-sim" && rm -f PokemonAccess-simulator.zip && zip -qr PokemonAccess-simulator.zip PokemonAccess.app && ls -la PokemonAccess-simulator.zip )
