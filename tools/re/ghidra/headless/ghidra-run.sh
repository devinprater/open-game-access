#!/usr/bin/env bash
# ghidra-run.sh — launch headless Ghidra with a correct JAVA_HOME.
#
# ⛔ WHY THIS WRAPPER EXISTS. JAVA_HOME on this machine pointed at JDK 17, and Ghidra
# 12.1.3 requires JDK 21. JAVA_HOME takes PRECEDENCE over PATH, so having JDK 21 on the
# PATH is not enough — Ghidra would start under 17 and fail with a version error that
# looks like a broken install. Every Ghidra invocation in this toolbox goes through here
# so the JDK choice is explicit rather than inherited from whatever the shell happens to
# have set.
set -uo pipefail

JDK21="${OGA_JDK21:-$HOME/scoop/apps/temurin21-jdk/current}"
export JAVA_HOME="$JDK21"
export PATH="$JAVA_HOME/bin:$PATH"

GHIDRA_HOME="${OGA_GHIDRA_HOME:-$HOME/scoop/apps/ghidra/current}"
SUPPORT="$GHIDRA_HOME/support"

usage() {
  cat <<'EOF'
usage: ghidra-run.sh <command> [args]

  headless <projdir> <projname> [analyzeHeadless args...]
        run analyzeHeadless
  pyghidra <binary> <script.py> [args...]
        run a CPython Ghidra script (PyGhidra)
  java          print the effective java version Ghidra will use
  check         verify the install: JDK version, Ghidra version, launcher presence
  gui           launch the Ghidra GUI
EOF
  exit 2
}

cmd="${1:-check}"; shift || true

echo "[ghidra-run] JAVA_HOME=$JAVA_HOME"
echo "[ghidra-run] GHIDRA=$GHIDRA_HOME"

case "$cmd" in
  java)
    "$JAVA_HOME/bin/java" -version
    ;;
  check)
    echo "--- java ---"
    "$JAVA_HOME/bin/java" -version 2>&1 | head -3
    jver=$("$JAVA_HOME/bin/java" -version 2>&1 | head -1 | sed -E 's/.*"([0-9]+).*/\1/')
    if [ "$jver" != "21" ]; then
      echo "!! JAVA_HOME is not JDK 21 (got '$jver') — Ghidra 12.1.3 requires 21."
      exit 1
    fi
    echo "--- ghidra ---"
    [ -f "$GHIDRA_HOME/Ghidra/application.properties" ] && \
      grep -E '^application.version|^application.release.name' \
           "$GHIDRA_HOME/Ghidra/application.properties" || echo "!! no application.properties"
    echo "--- launchers ---"
    for f in analyzeHeadless ghidraRun pyghidraRun; do
      [ -e "$SUPPORT/$f" ] && echo "  ok   $SUPPORT/$f" || echo "  MISSING $SUPPORT/$f"
    done
    echo "--- python for PyGhidra ---"
    python --version 2>&1
    ;;
  headless)
    projdir="${1:?need project dir}"; projname="${2:?need project name}"; shift 2
    "$SUPPORT/analyzeHeadless" "$projdir" "$projname" "$@"
    ;;
  pyghidra)
    bin="${1:?need binary}"; script="${2:?need script.py}"; shift 2
    [ -e "$SUPPORT/pyghidraRun" ] || { echo "!! no pyghidraRun — PyGhidra needs Ghidra 11.3+" >&2; exit 1; }
    "$SUPPORT/pyghidraRun" "$bin" "$script" "$@"
    ;;
  gui)
    "$SUPPORT/ghidraRun" &
    ;;
  *) usage ;;
esac
