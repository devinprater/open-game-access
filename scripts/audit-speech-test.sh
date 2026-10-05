#!/usr/bin/env bash
# audit-speech-test.sh — prove the adapter-speech gate actually catches what it claims.
#
# WHY: a gate that only ever passes is not a gate. Every other check in this repo proves
# itself by mutation (scripts/announce-test.sh rebuilds with a rule deleted and REQUIRES a
# failure). This does the same for the speech audit: it runs the checker against a
# deliberately-broken copy of an adapter and requires each violation class to be caught.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== the real adapters must PASS"
if ! python3 scripts/audit-adapter-speech.py --check Core > "$TMP/real.log" 2>&1; then
  echo "!! the real tree fails its own gate:"; cat "$TMP/real.log"; exit 1
fi
grep -q "PASS" "$TMP/real.log" || { echo "!! no PASS line"; cat "$TMP/real.log"; exit 1; }
echo "ok: real adapters pass"

# A minimal Core directory holding one deliberately-broken adapter per violation class.
mkdir -p "$TMP/Core"

# 1. a High line whose wording reads automatic
cat > "$TMP/Core/dq9_adapter.cpp" <<'EOF'
void F() {
    Say("Auto watch alert incoming.", "watch", oga::Priority::High);
}
EOF
if python3 scripts/audit-adapter-speech.py --check "$TMP/Core" > "$TMP/v1.log" 2>&1; then
  echo "!! the gate MISSED a High-but-automatic line"; exit 1
fi
grep -q "sounds automatic" "$TMP/v1.log" || { echo "!! wrong reason"; cat "$TMP/v1.log"; exit 1; }
echo "ok: caught a High line that sounds automatic"

# 2. an empty spoken string
cat > "$TMP/Core/dq9_adapter.cpp" <<'EOF'
void F() {
    Say("", "menu", oga::Priority::Normal);
}
EOF
if python3 scripts/audit-adapter-speech.py --check "$TMP/Core" > "$TMP/v2.log" 2>&1; then
  echo "!! the gate MISSED an empty spoken string"; exit 1
fi
grep -q "EMPTY spoken string" "$TMP/v2.log" || { echo "!! wrong reason"; cat "$TMP/v2.log"; exit 1; }
echo "ok: caught an empty spoken string"

# 3. a format specifier with nothing to fill it
cat > "$TMP/Core/dq9_adapter.cpp" <<'EOF'
void F() {
    Say("Enemy %s at %d.", "foe", oga::Priority::High);
}
EOF
if python3 scripts/audit-adapter-speech.py --check "$TMP/Core" > "$TMP/v3.log" 2>&1; then
  echo "!! the gate MISSED an unfilled format specifier"; exit 1
fi
grep -q "format specifier with no argument" "$TMP/v3.log" || { echo "!! wrong reason"; cat "$TMP/v3.log"; exit 1; }
echo "ok: caught an unfilled format specifier"

# 4. a CLEAN file must still pass -- otherwise the gate is just "always fail"
cat > "$TMP/Core/dq9_adapter.cpp" <<'EOF'
void F() {
    Say("Nobody nearby.", "nearby", oga::Priority::High);
    Say("Enemy phase.", "battle", oga::Priority::Normal);
}
EOF
if ! python3 scripts/audit-adapter-speech.py --check "$TMP/Core" > "$TMP/v4.log" 2>&1; then
  echo "!! the gate rejects a clean file (false positive):"; cat "$TMP/v4.log"; exit 1
fi
echo "ok: a clean adapter passes (no false positives)"

# 5. a line fed by a buffer variable must NOT be flagged: the specifier is filled by the
#    call, so this is correct code and the gate must not cry wolf.
cat > "$TMP/Core/dq9_adapter.cpp" <<'EOF'
void F() {
    char line[96];
    snprintf(line, sizeof(line), "Enemy %s at %d.", name, dist);
    Say(line, "foe", oga::Priority::High);
}
EOF
if ! python3 scripts/audit-adapter-speech.py --check "$TMP/Core" > "$TMP/v5.log" 2>&1; then
  echo "!! the gate flags a correctly-built line (false positive):"; cat "$TMP/v5.log"; exit 1
fi
echo "ok: a line built by snprintf into a buffer passes"

echo
echo "ALL AUDIT-SPEECH CASES CAUGHT"
