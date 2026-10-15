#!/usr/bin/env bash
# oga-speak.sh — speak one line on Windows, through the player's own reader.
#
# This is the default TTS sink for the PSP Steins;Gate reader:
#     node scripts/psp-sg-live.mjs --auto circle --tts "scripts/oga-speak.sh"
#
# ⭐ THE PROJECT RULE: speak through the USER'S OWN engine with their voice and
# rate settings — never override them — and keep the sink swappable.
#
# Two stages, in this order:
#
#   1. Prism (scripts/oga-speak-prism.py). Prism speaks through the screen
#      reader the player is already running: NVDA or JAWS when either is up,
#      OneCore or SAPI when neither is. The line comes out in their own voice,
#      at their own rate, and also lands on their braille display when the
#      engine drives one. Rate and voice are touched only if OGA_TTS_RATE is
#      set and non-zero.
#
#   2. SAPI via PowerShell, unchanged. Nothing to install, so the reader still
#      speaks on a machine with no python and no screen reader.
#
# WHY THE FALLBACK STAYS: stage 1 needs python plus the prismatoid wheel, stage
# 2 needs nothing. Stage 1 honours the rule where the pieces exist; stage 2
# keeps the reader testable end to end where they do not.
#
# ⛔ WSLENV, NOT JUST export: WSL does not hand the environment to Windows
# programs unless a variable is named in WSLENV. Without it the helper runs
# blind to OGA_TTS_RATE, OGA_SPEAK_BACKEND and OGA_SPEAK_DEBUG — measured, not
# assumed: `OGA_SPEAK_DEBUG=1 cmd.exe /c 'echo %OGA_SPEAK_DEBUG%'` prints
# `%OGA_SPEAK_DEBUG%`, and prints `1` once WSLENV names it.
#
# Rate: -10..10, the SAPI scale. Unset by default, which leaves the engine's
# own rate alone. Override with OGA_TTS_RATE.
#
# Python for stage 1: OGA_SPEAK_PYTHON, else %USERPROFILE%\.venvs\prism\
# Scripts\python.exe on the Windows side. Set OGA_SPEAK_DEBUG=1 to see which
# stage and backend was used on stderr.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER="$HERE/oga-speak-prism.py"
[ -n "${OGA_TTS_RATE:-}" ] && export OGA_TTS_RATE

# Read the whole line from stdin (UTF-8). Kept exactly as the reader wrote it:
# stage 1 passes it straight to the engine, so quotes and backticks in game
# text survive. Only the PowerShell fallback below needs them stripped.
LINE="$(cat)"
[ -z "${LINE// /}" ] && exit 0

# --- stage 1: Prism -----------------------------------------------------------
# The helper is a Windows program, so the settings have to be named in WSLENV
# to cross the boundary. Anything already in WSLENV is kept.
SHARE="${WSLENV:+$WSLENV:}OGA_TTS_RATE:OGA_SPEAK_BACKEND:OGA_SPEAK_DEBUG"

find_python() {
  local win_home candidate
  if [ -n "${OGA_SPEAK_PYTHON:-}" ] && [ -x "${OGA_SPEAK_PYTHON}" ]; then
    printf '%s' "$OGA_SPEAK_PYTHON"
    return 0
  fi
  win_home="$(wslpath -u "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')" 2>/dev/null)"
  if [ -n "$win_home" ]; then
    candidate="$win_home/.venvs/prism/Scripts/python.exe"
    if [ -x "$candidate" ]; then
      printf '%s' "$candidate"
      return 0
    fi
  fi
  command -v python.exe 2>/dev/null && return 0
  return 1
}

if [ -f "$HELPER" ] && PYTHON="$(find_python)"; then
  HELPER_WIN="$(wslpath -w "$HELPER" 2>/dev/null || true)"
  if [ -n "$HELPER_WIN" ]; then
    if printf '%s\n' "$LINE" | WSLENV="$SHARE" "$PYTHON" "$HELPER_WIN"; then
      exit 0
    fi
    [ -n "${OGA_SPEAK_DEBUG:-}" ] && echo "oga-speak.sh: Prism stage failed, using SAPI" >&2
  fi
fi

# --- stage 2: SAPI, no install required ---------------------------------------
RATE="${OGA_TTS_RATE:-0}"

# Strip anything that would break the PowerShell string literal — a quote or a
# backtick from game text would otherwise terminate the command and silently
# speak nothing.
LINE="${LINE//\"/\'}"
LINE="${LINE//\`/\'}"

exec powershell.exe -NoProfile -STA -Command \
  "Add-Type -AssemblyName System.Speech; \$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; \$s.Rate = ${RATE}; \$s.Speak([Console]::In.ReadToEnd())" <<< "$LINE"
