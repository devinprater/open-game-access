#!/usr/bin/env bash
# path-check-test.sh — prove the hardcoded-path gate catches a real break.
#
# ⛔ A gate that only ever passes is not a gate. This proves four things: a live dev path in
# a CI-INVOKED script is CAUGHT; a COMMENT naming the path is NOT (the lesson must be allowed
# to stay written down); a derived root PASSES; and a LOCAL-ONLY script is out of scope, so
# the ROM-sweep diagnostics that must name a path outside the repo are not punished.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== the real tree must PASS"
if ! python3 scripts/path-check.py > "$TMP/real.log" 2>&1; then
  echo "!! the real tree fails its own gate:"; cat "$TMP/real.log"; exit 1
fi
echo "ok: real tree passes"

# A scratch repo with a workflow that invokes one script, so that script is CI-reachable.
mkdir -p "$TMP/.github/workflows" "$TMP/scripts"
cat > "$TMP/.github/workflows/x.yml" <<'EOF'
jobs:
  t:
    steps:
      - run: bash scripts/candidate.sh
EOF
# The checker derives ROOT from its own location, so copy it into the scratch tree.
python3 - "$ROOT" "$TMP" <<'PY'
import pathlib, sys
root, tmp = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
src = (root/"scripts"/"path-check.py").read_text()
(tmp/"scripts"/"path-check.py").write_text(src)
PY
CHECK="$TMP/scripts/path-check.py"

# 1. a live absolute path in a CI-invoked script must be caught
printf '#!/usr/bin/env bash\ncd /home/someone/some-repo || exit 1\n' > "$TMP/scripts/candidate.sh"
if python3 "$CHECK" > "$TMP/v1.log" 2>&1; then
  echo "!! the gate MISSED a live hardcoded path"; exit 1
fi
grep -q "hardcodes a developer" "$TMP/v1.log" || { echo "!! wrong reason"; cat "$TMP/v1.log"; exit 1; }
echo "ok: caught a live hardcoded path in a CI-invoked script"

# 2. a COMMENT naming the path must NOT be flagged
printf '#!/usr/bin/env bash\n# the old version hardcoded /home/devin/oga-work and died in CI\n' \
  > "$TMP/scripts/candidate.sh"
if ! python3 "$CHECK" > "$TMP/v2.log" 2>&1; then
  echo "!! the gate flags a COMMENT (false positive) — it would punish recording the lesson:"
  cat "$TMP/v2.log"; exit 1
fi
echo "ok: a comment naming the path is not flagged"

# 3. a DERIVED root must pass
cat > "$TMP/scripts/candidate.sh" <<'EOF'
#!/usr/bin/env bash
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$R" || exit 1
EOF
if ! python3 "$CHECK" > "$TMP/v3.log" 2>&1; then
  echo "!! the gate rejects the CORRECT pattern:"; cat "$TMP/v3.log"; exit 1
fi
echo "ok: a derived root passes"

# 4. SCOPE: a script CI does NOT invoke may name a local path. ROMs must live outside the
#    repo by policy, so the sweep diagnostics can only use absolute paths — banning them
#    would make the gate noise nobody reads.
printf '#!/usr/bin/env bash\nROMDIR="/mnt/c/Users/Someone/Dropbox/games/GBA"\n' \
  > "$TMP/scripts/local-only.sh"
if ! python3 "$CHECK" > "$TMP/v4.log" 2>&1; then
  echo "!! the gate flags a LOCAL-ONLY diagnostic (out of scope):"; cat "$TMP/v4.log"; exit 1
fi
echo "ok: a local-only script is out of scope (no false positive)"

echo
echo "ALL PATH-CHECK CASES CAUGHT"
