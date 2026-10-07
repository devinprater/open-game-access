#!/usr/bin/env bash
# verify-ipa-test.sh — prove verify-ipa.sh CAN fail.
#
# ⛔ A GATE THAT ONLY EVER PASSES IS NOT A GATE. verify-ipa.sh exists because three real
# defects shipped through green builds (a simulator binary; an IPA with the core but no
# reader files; an IPA promised in release notes and never built). If the gate cannot
# catch those shapes, it is decoration that gets cited as proof.
#
# It needs no second build and no SDK: each case MUTATES the real IPA into the exact
# shape of a known-bad artifact and requires the gate to reject it.
#
# Usage: verify-ipa-test.sh [path/to/OpenGameAccess.ipa]
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IPA="${1:-$ROOT/xtool/OpenGameAccess.ipa}"
GATE="$ROOT/scripts/verify-ipa.sh"

[ -f "$IPA" ] || { echo "SKIP: no IPA at $IPA (build one first)"; exit 0; }
[ -f "$GATE" ] || { echo "!! no gate at $GATE" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== the real artifact must PASS (a self-test that only fails is not a check)"
if ! bash "$GATE" "$IPA" >/dev/null 2>&1; then
  echo "!! the gate rejects a GOOD ipa — it is broken, not strict" >&2
  bash "$GATE" "$IPA" 2>&1 | grep -E "FAIL" | sed 's/^/   /' >&2
  exit 1
fi
echo "   ok: the shipped IPA passes"

# mutate <name> <shell-commands-run-in-$APP>  -- builds a bad IPA, requires rejection
mutate() {
  local name="$1" body="$2"
  local d="$WORK/$name"
  rm -rf "$d"; mkdir -p "$d"
  unzip -q -o "$IPA" -d "$d" || { echo "   !! unzip failed"; return 1; }
  local APP="$d/Payload/OpenGameAccess.app"
  ( cd "$d" && APP="$APP" eval "$body" ) || true
  ( cd "$d" && zip -qr "$WORK/$name.ipa" Payload ) || return 1

  if bash "$GATE" "$WORK/$name.ipa" >/dev/null 2>&1; then
    echo "   !! NOT CAUGHT: $name" >&2
    return 1
  fi
  echo "   ok: rejected — $name"
  return 0
}

echo
echo "== each mutation must be REJECTED"
bad=0

# 1. The reader set gone. This is v0.6.0-nes: the core is in the binary and no reader
#    file is in the bundle, so every ROM boots silently.
mutate "reader-set-removed" \
  'rm -rf "$APP"/*.bundle/nes-lua' || bad=1

# 2. Game data present. The hard-stop rule: a stray ROM must never ship.
mutate "a-rom-inside" \
  'printf "\x00" > "$APP/leaked.gba"' || bad=1

# 3. The cue WAVs gone. The audio work depends on them; a working sink plays silence.
mutate "cue-wavs-removed" \
  'find "$APP" -name "*.wav" -delete' || bad=1

# 4. Signed. SideStore re-signs the app itself, which an embedded signature blocks.
mutate "signed-app" \
  'mkdir -p "$APP/_CodeSignature" && printf "x" > "$APP/_CodeSignature/CodeResources"' || bad=1

if [ "$bad" -ne 0 ]; then
  echo "!! verify-ipa.sh does not guard what it claims" >&2
  exit 1
fi

echo
echo "PASS: verify-ipa.sh accepts the shipped IPA and rejects every known-bad shape."
echo "      (The simulator-platform case needs a second build to mutate; it was proven"
echo "       directly — a simulator .app assembled into an .ipa is rejected on platform.)"
