#!/usr/bin/env bash
# lib-macho.sh — portable Mach-O platform detection. Source this, do not run it.
#
# ⛔ WHY THIS IS SHARED AND NOT INLINE. Two scripts need to answer "is this binary
# built for the simulator or the device?", and the first version of that check only
# looked for llvm-objdump. That tool exists in WSL (via the Swift toolchain) but NOT
# on a macOS runner, where the equivalent tools are otool and vtool. The check
# therefore returned "unknown" for a perfectly good simulator binary and the CI job
# died on a guard that was supposed to be protective.
#
# The guard is worth keeping: an earlier bug packaged a .dSYM DWARF companion file
# (same filename, valid Mach-O, device platform) as the app. So this detects across
# every available tool and normalises the answer, rather than trusting one binary.
#
# macho_platform <file> -> iossimulator | ios | macos | unknown
macho_platform() {
  local file="$1"
  local raw=""
  [ -f "$file" ] || { echo unknown; return 0; }

  # 1. llvm-objdump (WSL/Linux, and macOS when llvm is installed)
  local cand
  for cand in /usr/local/swift/bin/llvm-objdump llvm-objdump; do
    if command -v "$cand" >/dev/null 2>&1; then
      raw="$("$cand" --macho --private-headers "$file" 2>/dev/null \
             | awk '/LC_BUILD_VERSION/{seen=1} seen && /platform/{print $2; exit}')"
      [ -n "$raw" ] && break
    fi
  done

  # 2. vtool (macOS, part of Xcode)
  if [ -z "$raw" ] && command -v vtool >/dev/null 2>&1; then
    raw="$(vtool -show-build "$file" 2>/dev/null \
           | awk '/platform/{print $2; exit}')"
  fi

  # 3. otool (macOS, ships with Xcode)
  if [ -z "$raw" ] && command -v otool >/dev/null 2>&1; then
    raw="$(otool -l "$file" 2>/dev/null \
           | awk '/LC_BUILD_VERSION/{seen=1} seen && /platform/{print $2; exit}')"
  fi

  # Normalise. Tools disagree on the spelling: llvm-objdump says "iossimulator",
  # otool/vtool say "IOS_SIMULATOR" on newer Xcode and a bare number on older.
  case "$raw" in
    *IOSSIMULATOR*|*iossimulator*|7) echo iossimulator ;;
    *MACOS*|*macos*|1)               echo macos ;;
    *IOS*|*ios*|2)                   echo ios ;;
    *)                               echo unknown ;;
  esac
}

# macho_platform_raw <file> -> whatever the tool printed, for diagnostics
macho_platform_raw() {
  local file="$1" cand
  for cand in /usr/local/swift/bin/llvm-objdump llvm-objdump; do
    if command -v "$cand" >/dev/null 2>&1; then
      "$cand" --macho --private-headers "$file" 2>/dev/null \
        | awk '/LC_BUILD_VERSION/{seen=1} seen && /platform/{print; exit}'
      return 0
    fi
  done
  command -v otool >/dev/null 2>&1 && \
    otool -l "$file" 2>/dev/null | awk '/LC_BUILD_VERSION/{seen=1} seen && /platform/{print; exit}'
  return 0
}

# macho_platform_tools -> which detection tools are present (for diagnostics)
macho_platform_tools() {
  local out=""
  for cand in /usr/local/swift/bin/llvm-objdump llvm-objdump vtool otool; do
    command -v "$cand" >/dev/null 2>&1 && out="$out $cand"
  done
  echo "${out:- none}"
}
