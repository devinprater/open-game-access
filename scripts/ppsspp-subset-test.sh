#!/usr/bin/env bash
# ppsspp-subset-test.sh — every file the audited PPSSPP subset names must exist
# in the fetched tree. This is the CI guard against upstream layout drift: if
# the PPSSPP pin moves (or a fetch is shallow/broken), the iOS build would
# otherwise fail hundreds of lines deep in compile() with a confusing
# "No such file" instead of here, naming the missing path.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/scripts/core-sources.sh"
PPSPP_SRC="${PPSPP_SRC:-$HOME/src/ppsspp}"
fail=0
check() { # check <list-var-name> <subdir-prefix>
  local var="$1" prefix="$2" f n=0
  for f in ${!var}; do
    n=$((n + 1))
    if [ ! -f "$PPSPP_SRC/$prefix$f" ]; then
      echo "FAIL: $var names '$prefix$f' but it is missing from $PPSPP_SRC" >&2
      fail=1
    fi
  done
  echo "ok: $var ($n files present)"
}
[ -d "$PPSPP_SRC/Core" ] || { echo "FAIL: no PPSSPP tree at $PPSPP_SRC (run scripts/bootstrap-deps.sh)" >&2; exit 1; }
check PPSPP_CORE ""
check PPSPP_EXT_CPP ""
check PPSPP_EXT_C ""
check PPSPP_X86 ""
check PPSPP_ARM ""
check PPSPP_X86_ASM ""
n=0
for f in $PPSPP_LUA; do
  n=$((n + 1))
  [ -f "$PPSPP_SRC/ext/lua/$f" ] || { echo "FAIL: PPSPP_LUA names 'ext/lua/$f' but it is missing" >&2; fail=1; }
done
echo "ok: PPSPP_LUA ($n files present)"
n=0
for f in $PPSPP_ASSETS; do
  n=$((n + 1))
  [ -f "$PPSPP_SRC/assets/$f" ] || { echo "FAIL: PPSPP_ASSETS names 'assets/$f' but it is missing" >&2; fail=1; }
done
echo "ok: PPSPP_ASSETS ($n files present)"
for f in $PPSPP_GLUE; do
  [ -f "$ROOT/Core/$f" ] || { echo "FAIL: PPSPP_GLUE names 'Core/$f' but it is missing" >&2; fail=1; }
done
echo "ok: PPSPP_GLUE present"
[ "$fail" -eq 0 ] || exit 1
echo "PASS: PPSSPP subset exists in the fetched tree"
