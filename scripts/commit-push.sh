#!/usr/bin/env bash
# commit-push.sh — commit the staged tree and push, with the guard in front.
set -euo pipefail
cd "$HOME/oga-work" || exit 2

echo "== guard on the tree about to be pushed =="
bash "/mnt/c/Users/Devin Prater/pokemon-access-ios/scripts/check-no-roms.sh" "$PWD"

echo
git add -A
echo "staged files: $(git diff --cached --name-only | wc -l)"

# Hard refuse: nothing game-shaped may ever be pushed.
BAD="$(git diff --cached --name-only | grep -iE '\.(nds|dsi|gba|gbc|gb|sav|srm|dsv|state|apk|ipa|a|o|so|ram)$|bios[0-9]?\.bin|firmware\.bin' || true)"
if [ -n "$BAD" ]; then
  echo "!! REFUSING TO PUSH — game data or build output staged:"; echo "$BAD"; exit 1
fi
echo "  clean"

git commit -q -m "$(cat <<'EOF'
Fix build scripts for CI; add the Game Boy / GBC / GBA reader set

Two CI-breaking assumptions in the build scripts:

1. 49 scripts hard-coded ROOT=$HOME/pokemon-access-ios — the LOCAL directory name.
   CI checks the repo out as open-game-access, so every path was wrong, and the
   failure surfaced as a missing SDK rather than a missing repo root. They now
   derive the root from ${BASH_SOURCE[0]}, as build-core.sh already did.

2. build-sim.sh hard-coded the xtool-fetched Darwin SDK path AND version
   (iPhoneSimulator26.5.sdk). A macOS runner has Xcode's own SDK at a different
   path and version, so the build failed with "no iPhoneSimulator SDK at ..." while
   an SDK was right there. SDKROOT, CXX and CC are now environment-overridable with
   the local values as defaults, so one script serves both environments.

Also:
- scripts/stage-all.sh: the full staging pipeline in one order-correct step, and
  stage-repo.sh no longer wipes .git out of the tree it stages into.
- The Game Boy / GBC / GBA reader set (162 Lua files: Crystal, FireRed/LeafGreen,
  Ruby/Sapphire/Emerald, Gold/Silver/Crystal, and the shared gb/gba/a-star/serpent
  helpers) plus its 33 earcon sounds, credited in the README.
EOF
)"
echo "committed: $(git log --oneline -1)"

echo
echo "== pushing =="
git push origin HEAD:main 2>&1 | tail -6
echo
echo "== remote state =="
git ls-remote --heads origin | head -3
