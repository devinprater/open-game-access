#!/usr/bin/env bash
# ghidra-headless.sh — run analyzeHeadless correctly from git-bash on Windows.
#
# ⛔ TWO TRAPS THIS WRAPPER EXISTS TO AVOID, both of which cost real time:
#
# 1. java is a NATIVE Windows program and receives UNTRANSLATED paths. MSYS path
#    conversion is disabled on this host, so handing `support/analyzeHeadless` an
#    MSYS path like /c/Users/... makes java fail with
#    `ClassNotFoundException: LaunchSupport` — which reads as a broken Ghidra install
#    and is really a path-format problem. Use the .bat launcher and Windows-style
#    paths (C:\Users\...) for anything java touches.
#
# 2. JAVA_HOME must be set IN THE INVOKING ENVIRONMENT, not just session-wide with
#    setx. `setx` only affects processes started afterwards, so a shell that already
#    exists keeps the old value and Ghidra reports "JDK 21+ could not be found" while
#    JDK 21 sits right there. Ghidra's launcher reads JAVA_HOME and ignores the PATH,
#    so having JDK 21 on the PATH is not enough.
set -uo pipefail

JDK21_WIN="${OGA_JDK21_WIN:-C:\\Users\\Devin Prater\\scoop\\apps\\temurin21-jdk\\current}"
GHIDRA_HOME_WIN="${OGA_GHIDRA_WIN:-C:\\Users\\Devin Prater\\scoop\\apps\\ghidra\\current}"

export JAVA_HOME="$JDK21_WIN"

usage() {
  cat <<'EOF'
usage: ghidra-headless.sh <project-dir> [options...]

  <project-dir>   Windows-style path to the project directory, e.g.
                  'C:\Users\Devin Prater\oga-ghidra\proj'
  [options...]    passed straight to analyzeHeadless, e.g.
                    -import  <binary>
                    -process <program>
                    -postScript <script.py>
                    -scriptPath <dir>
                    -deleteProject
                    -analysisTimeoutPerFile 600

  info            print the toolchain Ghidra will use
EOF
  exit 2
}

if [ "${1:-}" = "info" ]; then
  echo "JAVA_HOME=$JAVA_HOME"
  echo "GHIDRA=$GHIDRA_HOME_WIN"
  cmd.exe /c "\"$GHIDRA_HOME_WIN\\support\\analyzeHeadless.bat\" 2>&1 | head -5" 2>&1 | head -6
  exit 0
fi

PROJDIR="${1:?need a project dir (Windows-style path)}"; shift
bash -c "cd '$GHIDRA_HOME_WIN/support' && cmd.exe /c \"analyzeHeadless.bat '$PROJDIR' $*\""
