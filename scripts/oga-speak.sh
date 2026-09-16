#!/usr/bin/env bash
# oga-speak.sh — speak one line on Windows using the OS speech engine (SAPI).
#
# This is the default TTS sink for the PSP Steins;Gate reader:
#     node scripts/psp-sg-live.mjs --auto circle --tts "scripts/oga-speak.sh"
#
# ⭐ WHY A SEPARATE SCRIPT: the reader pipes the line on stdin and the project rule is to
# speak through the USER'S OWN engine with their voice/rate settings — never to override
# them. Swapping this one file swaps the voice, without touching the reader.
#
# ⚠ On a screen-reader setup you may want to route through the screen reader instead
# (NVDA's controller client, or `say` on macOS). This uses SAPI because it needs no
# install, which makes the reader testable end to end.
#
# Rate: -10..10. Default 0. Override with OGA_TTS_RATE.
set -uo pipefail

RATE="${OGA_TTS_RATE:-0}"

# Read the whole line from stdin (UTF-8), then strip anything that would break the
# PowerShell string literal — a quote or a backtick from game text would otherwise
# terminate the command and silently speak nothing.
LINE="$(cat)"
LINE="${LINE//\"/\'}"
LINE="${LINE//\`/\'}"

[ -z "${LINE// /}" ] && exit 0

# SAPI.SpVoice via PowerShell. -NoProfile keeps it fast; STA is required by SAPI.
exec powershell.exe -NoProfile -STA -Command \
  "Add-Type -AssemblyName System.Speech; \$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; \$s.Rate = ${RATE}; \$s.Speak([Console]::In.ReadToEnd())" <<< "$LINE"
