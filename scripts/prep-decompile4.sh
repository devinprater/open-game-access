#!/bin/bash
# Compile-check DisDecompile4.java, then run it against the DISSIDIA_ELF project.
set -eu
GH="/mnt/c/Users/Devin Prater/oga-ghidra-dissidia"
CPF="/mnt/c/Users/Devin Prater/AppData/Local/Temp/ghidra-cp/cp.txt"
OUT="/mnt/c/Users/Devin Prater/AppData/Local/Temp/ghidra-cp/out"

CP=$(cat "$CPF")
cd "$GH"
javac -nowarn -cp "$CP" -d "$OUT" DisDecompile4.java 2>&1 | head -12
if [ -f "$OUT/DisDecompile4.class" ]; then echo "COMPILED OK"; else echo "COMPILE FAILED"; exit 1; fi

cat > run-decompile4.bat <<'EOF'
@echo off
setlocal
set GHIDRA_HOME=%USERPROFILE%\scoop\apps\ghidra\current
set PROJDIR=%USERPROFILE%\oga-ghidra-dissidia
"%GHIDRA_HOME%\support\analyzeHeadless.bat" "%PROJDIR%" DISSIDIA_ELF ^
  -process EBOOT.dec ^
  -noanalysis ^
  -scriptPath "%PROJDIR%" ^
  -postScript DisDecompile4.java
endlocal
EOF
echo "wrapper written"
