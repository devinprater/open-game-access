#!/usr/bin/env bash
# stage-tt-session.sh — copy THIS session's Tag Team work from the Windows tree into the git checkout.
#
# WHY THIS EXISTS
#   There are two divergent trees:
#     /mnt/c/Users/Devin Prater/open-game-access   <- NO .git; holds the session's work
#     /home/devin/oga-work                         <- the git repo (origin git@github-oga:...)
#   They are NOT mirrors (verified by inode/device and a marker-file test). Nothing in this script
#   runs automatically: it only PRINTS by default. Pass --apply to actually copy.
#
# SAFETY
#   * dry-run by default; --apply required to copy
#   * never commits, never pushes, never touches the repo's .git
#   * never deletes anything
#   * refuses to run if a destination file exists with DIFFERENT content unless --overwrite
#
# USAGE
#   bash scripts/stage-tt-session.sh              # show what would be copied
#   bash scripts/stage-tt-session.sh --apply      # copy new/changed files into the repo
#   bash scripts/stage-tt-session.sh --apply --overwrite
#
# AFTER RUNNING
#   cd /home/devin/oga-work && git status     # review, then stage/commit yourself

set -uo pipefail

SRC="/mnt/c/Users/Devin Prater/open-game-access"
DST="/home/devin/oga-work"
APPLY=0
OVERWRITE=0
for a in "$@"; do
  case "$a" in
    --apply) APPLY=1 ;;
    --overwrite) OVERWRITE=1 ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown arg: $a" >&2; exit 2 ;;
  esac
done

# The session's work: the Tag Team doc, its scripts, and the Ghidra analysis scripts.
FILES=(
  "docs/reverse-engineering/dbz-tenkaichi-tag-team.md"

  "scripts/psp-tt-hp.mjs"          "scripts/psp-tt-advance.mjs"      "scripts/psp-tt-route.mjs"
  "scripts/psp-tt-freeze.mjs"      "scripts/psp-tt-movetest.mjs"     "scripts/psp-tt-charbid.mjs"
  "scripts/psp-tt-charbid2.mjs"    "scripts/psp-tt-autoadvance.mjs"  "scripts/psp-tt-livewatch.mjs"
  "scripts/psp-tt-fieldpos.mjs"    "scripts/psp-tt-postrack.mjs"     "scripts/psp-tt-stagesearch.mjs"
  "scripts/psp-tt-curline.mjs"     "scripts/psp-tt-cursorfind.mjs"   "scripts/psp-tt-posbidi.mjs"
  "scripts/psp-tt-reader.mjs"
  "scripts/psp-shot.py"            "scripts/psp-ram-textsearch.mjs"
  "scripts/psp-msgids.py"          "scripts/psp-packfile-find.py"    "scripts/psp-packfile-strings.py"
  "scripts/psp-elf-window.py"      "scripts/oga-cso-extract.py"
  "scripts/psp-sg-live.mjs"        "scripts/psp-sg-broaddiff.mjs"

  "scripts/psp-tt-beacon.py"       "scripts/psp-tt-indicators.py"
  "scripts/psp-tt-glyph.py"        "scripts/psp-tt-glyph-validate.py"
  "scripts/psp-tt-pos.py"           "scripts/psp-tt-waypoint.py"
  "scripts/psp-tt-objective.py"     "scripts/psp-tt-autotravel.py"
  "scripts/psp-tt-glyph.py"         "scripts/psp-tt-glyph-validate.py"
  "scripts/TagTeamQuery.java"      "scripts/TagTeamTrace.java"       "scripts/TagTeamNames.java"
  "scripts/TagTeamIdentityPath.java" "scripts/TagTeamIdTable.java"   "scripts/TagTeamF26400.java"
)

# Probe outputs kept as EVIDENCE for the doc's claims. Suggest ignoring, do not copy by default.
SCRATCH=(
  "tagteam-resolver-out.txt"  "tagteam-identitypath-out.txt"  "tagteam-f26400-out.txt"
  "tagteam-names-out.txt"     "tagteam-datarefs-out.txt"      "tagteam-idtable-out.txt"
  "gbframe.ppm"
)

echo "source : $SRC"
echo "dest   : $DST"
echo "mode   : $([ $APPLY -eq 1 ] && echo APPLY || echo 'DRY RUN (pass --apply to copy)')"
echo

[ -d "$SRC" ] || { echo "FAIL: source missing"; exit 1; }
[ -d "$DST" ] || { echo "FAIL: dest missing"; exit 1; }
[ -d "$DST/.git" ] || { echo "FAIL: $DST is not a git checkout"; exit 1; }

copied=0; skipped=0; missing=0
for rel in "${FILES[@]}"; do
  s="$SRC/$rel"; d="$DST/$rel"
  if [ ! -f "$s" ]; then echo "  MISSING SRC  $rel"; missing=$((missing+1)); continue; fi
  if [ -f "$d" ] && cmp -s "$s" "$d"; then echo "  identical    $rel"; skipped=$((skipped+1)); continue; fi
  if [ -f "$d" ] && [ $OVERWRITE -eq 0 ]; then
    echo "  DIFFERS      $rel   (dest differs; use --overwrite to replace)"
    skipped=$((skipped+1)); continue
  fi
  if [ $APPLY -eq 1 ]; then
    mkdir -p "$(dirname "$d")"
    cp -p "$s" "$d" && { echo "  copied       $rel"; copied=$((copied+1)); }
  else
    echo "  would copy   $rel"; copied=$((copied+1))
  fi
done

echo
echo "files: would-copy/copied=$copied  skipped=$skipped  missing=$missing"
echo
echo "--- probe outputs (evidence, NOT copied; consider .gitignore) ---"
for rel in "${SCRATCH[@]}"; do [ -f "$SRC/$rel" ] && echo "  $rel"; done
echo
if [ $APPLY -eq 1 ]; then
  echo "NEXT (yours to run):"
  echo "  cd $DST && git status && git add <paths> && git commit"
else
  echo "Nothing was changed. Re-run with --apply to copy."
fi
