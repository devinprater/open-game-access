#!/usr/bin/env bash
# oga-ghidra-smoke.sh — prove headless Ghidra actually runs, and that PyGhidra works.
#
# ⛔ A "Ghidra is installed" claim is worthless until a headless command has actually
# returned output. This runs analyzeHeadless for real and reports what it printed, plus
# the PyGhidra version, because Ghidra 12.x defaults to PyGhidra and an agent workflow
# depends on it working from a CLI.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
RUN="$ROOT/tools/re/ghidra/headless/ghidra-run.sh"
WORK="${OGA_GHIDRA_WORK:-$HOME/oga-ghidra}"
mkdir -p "$WORK"

echo "===== 1. install check ====="
bash "$RUN" check 2>&1 | tail -14

echo
echo "===== 2. PyGhidra version ====="
# ⛔ Use the Python that PyGhidra is actually installed for. On this machine `python` on
# PATH resolves to Hermes's own venv (3.11); the user's Python 3.14 is a separate
# install. Report both so the difference is visible rather than mysterious.
echo "-- python on PATH --"
python --version 2>&1
python -c "import pyghidra; print('pyghidra', pyghidra.__version__)" 2>&1 | tail -1
echo "-- python 3.14 (via py launcher) --"
py -V:3.14 --version 2>&1
py -V:3.14 -c "import pyghidra; print('pyghidra', pyghidra.__version__)" 2>&1 | tail -1

echo
echo "===== 3. analyzeHeadless smoke test ====="
# No binary to import yet, so run the smallest real invocation: create/load a project.
# This exercises the JVM, the Ghidra classpath and the project db — the parts that
# actually break when JAVA_HOME is wrong.
OUT="$WORK/headless-smoke.log"
bash "$RUN" headless "$WORK/proj" OgaSmoke -import /dev/null > "$OUT" 2>&1 || true
echo "exit=$? (an import failure is expected — we are testing that Ghidra starts)"
echo "-- first lines --"
head -6 "$OUT" 2>/dev/null
echo "-- errors mentioning Java/Ghidra startup --"
grep -iE 'ERROR|Exception|Unable|requires|not found' "$OUT" 2>/dev/null | head -8
echo "-- report --"
grep -iE 'REPORT|Ghidra Version|JVM|java.version' "$OUT" 2>/dev/null | head -6
