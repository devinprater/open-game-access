#!/usr/bin/env bash
# verify-ipa.sh — prove the packaged .ipa is a DEVICE app that can be sideloaded.
#
# ⛔ WHY THIS IS NOT THE SAME AS "THE BUILD SUCCEEDED". A green build says the compiler
# and linker were happy; it says nothing about what got packaged. Measured failures in
# this project's history that every build reported as success:
#
#   * a SIMULATOR binary (platform iossimulator) — installs on nothing;
#   * a v0.6.0-nes IPA with the Mesen core linked in and ZERO reader files bundled, so
#     every ROM booted silently;
#   * an IPA the release notes claimed existed, that was never built at all.
#
# So this asserts on the UNPACKED artifact: platform, signature, the reader sets, the
# cue binding the audio work depends on, and that no game data rode along.
#
# Usage: verify-ipa.sh [path/to/OpenGameAccess.ipa]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IPA="${1:-$ROOT/xtool/OpenGameAccess.ipa}"

[ -f "$IPA" ] || { echo "!! no IPA at $IPA" >&2; exit 1; }

OBJDUMP=""
for d in /usr/local/swift/bin /usr/bin /opt/homebrew/opt/llvm/bin; do
  [ -x "$d/llvm-objdump" ] && [ -z "$OBJDUMP" ] && OBJDUMP="$d/llvm-objdump"
done
[ -z "$OBJDUMP" ] && OBJDUMP="$(command -v llvm-objdump || true)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== unpacking $IPA"
unzip -q -o "$IPA" -d "$WORK" || { echo "!! not a readable zip" >&2; exit 1; }
APP="$(find "$WORK/Payload" -maxdepth 1 -name '*.app' -type d | head -1)"
[ -n "$APP" ] || { echo "!! no .app inside Payload/" >&2; exit 1; }
BIN="$(find "$APP" -maxdepth 1 -type f -perm -u+x ! -name '*.dylib' | head -1)"
[ -n "$BIN" ] || { echo "!! no executable in $APP" >&2; exit 1; }

fail=0
note() { printf '   %-6s %s\n' "$1" "$2"; }

echo "== platform"
if [ -n "$OBJDUMP" ]; then
  PLATFORM="$("$OBJDUMP" --macho --private-headers "$BIN" 2>/dev/null \
    | grep -A3 'LC_BUILD_VERSION' | awk '/platform/{print $2}' | head -1)"
  case "$PLATFORM" in
    ios) note ok "platform ios (installs on a device)" ;;
    iossimulator) note FAIL "platform iossimulator — this IPA cannot be installed"; fail=1 ;;
    "") note FAIL "could not read LC_BUILD_VERSION"; fail=1 ;;
    *) note FAIL "unexpected platform '$PLATFORM'"; fail=1 ;;
  esac
else
  note WARN "no llvm-objdump; platform NOT verified"
fi

echo "== signature (unsigned is what lets SideStore re-sign it)"
if [ -d "$APP/_CodeSignature" ]; then
  note FAIL "the app carries _CodeSignature — not the unsigned artifact we ship"; fail=1
else
  note ok "no _CodeSignature"
fi
if [ -f "$APP/embedded.mobileprovision" ]; then
  note FAIL "the app carries embedded.mobileprovision"; fail=1
else
  note ok "no embedded.mobileprovision"
fi

echo "== bundled reader sets (a core in the binary is NOT a reader in the bundle)"
for d in gba-lua nes-lua; do
  n="$(find "$APP" -type d -name "$d" | head -1)"
  if [ -n "$n" ]; then note ok "$d ($(find "$n" -type f | wc -l) files)"; else note FAIL "MISSING $d"; fail=1; fi
done

# The reader's own entry points, by name: a directory can exist and be empty.
for f in gba-lua/oga_bootstrap.lua gba-lua/mgba_compat.lua gba-lua/pokemon.lua; do
  if find "$APP" -path "*$f" | grep -q .; then note ok "$f"; else note FAIL "MISSING $f"; fail=1; fi
done

# The positional-cue WAVs, and the binding that plays them.
wavs="$(find "$APP" -name '*.wav' | wc -l)"
if [ "$wavs" -ge 33 ]; then note ok "$wavs cue WAVs"; else note FAIL "only $wavs cue WAVs (expected >= 33)"; fail=1; fi
# ⛔ WRITE THE STRING LIST TO A FILE, NEVER PIPE IT INTO `grep -q`. `grep -q` exits at
# the first match, so `strings` dies of SIGPIPE; under `pipefail` the pipeline status
# becomes 141 and a PRESENT string reports as absent. Measured on a good IPA.
STRINGS="$WORK/strings.txt"
strings -n 4 "$BIN" > "$STRINGS" 2>/dev/null || true
if grep -qx "oga_play_sound" "$STRINGS"; then
  note ok "oga_play_sound binding present"
else
  note FAIL "the cue binding is absent from the binary"; fail=1
fi

echo "== no game data (the hard-stop rule)"
bad="$(find "$APP" -iregex '.*\.\(gba\|gbc\|gb\|nes\|nds\|sfc\|smc\|iso\|cso\|sav\|srm\)$' | head -5)"
if [ -n "$bad" ]; then
  note FAIL "game data found:"; echo "$bad" | sed 's/^/          /'; fail=1
else
  note ok "no ROMs or save files"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "PASS: $IPA is a device IPA, unsigned, with its reader sets and cue audio."
else
  echo "!! verify-ipa FAILED" >&2
  exit 1
fi
