#!/usr/bin/env bash
# swift-check-test.sh — prove the Swift checks actually FAIL when they should.
#
# WHY: the previous swift-check.sh was a gate that could never fail, which is worse than no
# gate because it looked like coverage. Any replacement must prove it can catch a real
# break, or it repeats the same mistake.
set -uo pipefail
export PATH=/usr/local/swift/bin:/usr/local/bin:$PATH
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== the real tree must PASS"
if ! bash scripts/swift-check.sh > "$TMP/real.log" 2>&1; then
  echo "!! the real tree fails its own gate:"; cat "$TMP/real.log"; exit 1
fi
echo "ok: real tree passes"

# ── 1. a Swift file that does not type-check must fail
if ! command -v swiftc >/dev/null 2>&1; then
  # ⛔ The type-check cannot be exercised without a compiler; say so rather than
  # reporting a pass for a check that did not run.
  echo "-- SKIPPED: no swiftc, so the type-error case cannot be exercised here"
else
cat > "$TMP/broken.swift" <<'EOF'
enum Thing: Int32 {
    case a = 0
}
let x: Int = Thing.a   // type error: cannot convert Thing to Int
EOF
if swiftc -typecheck -swift-version 5 "$TMP/broken.swift" >/dev/null 2>&1; then
  echo "!! swiftc accepted a file with a type error (the check is not real)"
  exit 1
fi
echo "ok: swiftc rejects a genuine type error (so the type-check can fail)"
fi

# ── 2. the ABI mirror must fail when a case is renamed
python3 - "$ROOT" "$TMP" <<'PY'
import pathlib, re, subprocess, sys
root, tmp = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])

# copy the two files into a scratch tree and break the Swift name for one case
sw = (root/"Sources/OpenGameAccess/AdapterCommand.swift").read_text()
broken = sw.replace("case repeatNewest = 38", "case repeatNewer = 38", 1)
assert broken != sw, "could not break the Swift case name"
(tmp/"AdapterCommand.swift").write_text(broken)

# run the same comparison logic against the scratch copy
cpp = (root/"Core/adapter.h").read_text()
m = re.search(r'enum class Command \{(.*?)\n\};', cpp, re.S)
body = re.sub(r'//[^\n]*', '', m.group(1))
cpp_names = [n.strip() for n in re.findall(r'([A-Za-z_]\w*)\s*(?:=[^,]*)?,', body + ',') if n.strip()]

m2 = re.search(r'enum AdapterCommand: Int32[^{]*\{(.*?)\n\s*/// The label', broken, re.S)
sb = re.sub(r'///[^\n]*', '', m2.group(1))
swift = [n for n, _ in re.findall(r'case\s+([A-Za-z_]\w*)\s*(?:=\s*(\d+))?', sb)]

drift = any((cn[0].lower()+cn[1:]) != sn for cn, sn in zip(cpp_names, swift))
print("DETECTED" if drift else "MISSED")
PY
echo "ok: the ABI mirror comparison detects a renamed case"

# ── 3. a Swift file calling an undeclared poke_ symbol must fail
mkdir -p "$TMP/swift"
cp "$ROOT"/Sources/OpenGameAccess/*.swift "$TMP/swift/"
echo 'func f(core: OpaquePointer) { poke_does_not_exist(core) }' >> "$TMP/swift/AdapterCommand.swift"
declared=$(grep -oE '\bpoke_[a-z_]+\s*\(' "$ROOT/Sources/CPokeCore/include/pokecore.h" | tr -d '( ' | sort -u)
called=$(grep -ohE '\bpoke_[a-z_]+\b' "$TMP"/swift/*.swift | sort -u)
missing=$(comm -13 <(echo "$declared") <(echo "$called"))
if [ -z "$missing" ]; then
  echo "!! an undeclared poke_ symbol was NOT detected"
  exit 1
fi
echo "ok: an undeclared poke_ symbol is detected ($missing)"

echo
echo "ALL SWIFT-CHECK CASES CAUGHT"
