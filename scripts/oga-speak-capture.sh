#!/usr/bin/env bash
# test sink: append each line it is asked to speak, so the reader's TTS wiring can be
# verified objectively (order, completeness, no duplicates) without listening.
set -uo pipefail
OUT="${OGA_TTS_CAPTURE:?set OGA_TTS_CAPTURE to a file path}"
LINE="$(cat)"
printf '%s\n' "$LINE" >> "$OUT"
