#!/usr/bin/env python3
"""oga-speak-prism.py - speak one line of game text through Prism.

WHY THIS EXISTS: the OGA readers hand one finished line to a sink program and
expect it to come out in the player's own voice, at their own rate. Prism is
the only layer that reaches NVDA or JAWS when either is running, falls back to
SAPI or OneCore when neither is, and puts the same line on a braille display
when the chosen engine drives one.

The line arrives on stdin, unchanged: no quote stripping, because nothing here
goes through a shell string literal. Speech is waited out before exiting, so
the caller's one-line-per-process pacing still holds.

Exit codes
    0   the line was handed to an engine and was allowed to finish
    3   Prism is missing or no engine could be reached; the caller falls back
        to the SAPI path in oga-speak.sh

Environment
    OGA_SPEAK_BACKEND   force one backend by name (NVDA, JAWS, OneCore, SAPI...)
    OGA_TTS_RATE        -10..10, the same scale oga-speak.sh uses for SAPI.
                        Unset or 0 leaves the engine alone, which keeps the
                        reader's own rate, as the project rule requires.
    OGA_SPEAK_DEBUG     print decisions to stderr
"""

from __future__ import annotations

import os
import sys
import time

EXIT_OK = 0
EXIT_NO_PRISM = 3

# Preference order: the readers a player is most likely running, then the two
# OS engines that need no reader at all.
PREFERRED = ("NVDA", "JAWS", "OneCore", "SAPI")

# Pacing for engines that cannot report speech state. The per-byte figure
# matches the queue's own estimate in Core/announce.h so the two agree.
EST_BASE_MS = 300
EST_MS_PER_BYTE = 55
MAX_WAIT_S = 30.0
START_WAIT_S = 1.0


def log(message: str) -> None:
    if os.environ.get("OGA_SPEAK_DEBUG"):
        print(f"oga-speak-prism: {message}", file=sys.stderr)


def estimate_seconds(text: str) -> float:
    return min((EST_BASE_MS + EST_MS_PER_BYTE * len(text.encode("utf-8"))) / 1000.0, MAX_WAIT_S)


def choose_backend(ctx):
    forced = os.environ.get("OGA_SPEAK_BACKEND", "").strip()
    for name in ([forced] if forced else PREFERRED):
        try:
            backend = ctx.acquire(ctx.id_of(name))
            features = backend.features
        except Exception as exc:  # backend not installed or not running
            log(f"{name}: unavailable ({exc})")
            continue
        if not features.is_supported_at_runtime:
            log(f"{name}: present but not running")
            continue
        if not features.supports_speak:
            log(f"{name}: cannot speak")
            continue
        log(f"{name}: chosen")
        return backend, name
    return None, None


def apply_rate(backend) -> None:
    raw = os.environ.get("OGA_TTS_RATE", "").strip()
    if not raw:
        return
    try:
        rate = float(raw)
    except ValueError:
        log(f"OGA_TTS_RATE={raw!r} is not a number; ignored")
        return
    if rate == 0.0:
        return  # leave the engine's own rate alone
    rate = max(-10.0, min(10.0, rate))
    if not backend.features.supports_set_rate:
        log("engine has no rate control; leaving it alone")
        return
    try:
        backend.rate = (rate + 10.0) / 20.0  # -10..10 onto Prism's 0..1
        log(f"rate set to {rate}")
    except Exception as exc:
        log(f"rate rejected: {exc}")


def wait_for_speech(backend, text: str) -> None:
    if not backend.features.supports_is_speaking:
        time.sleep(estimate_seconds(text))
        return
    deadline = time.monotonic() + START_WAIT_S
    started = False
    while time.monotonic() < deadline:
        try:
            if backend.speaking:
                started = True
                break
        except Exception as exc:
            log(f"speech state unreadable ({exc}); pacing by estimate")
            time.sleep(estimate_seconds(text))
            return
        time.sleep(0.03)
    if not started:
        # Either the engine never reports state or the line finished instantly.
        time.sleep(estimate_seconds(text))
        return
    deadline = time.monotonic() + MAX_WAIT_S
    while time.monotonic() < deadline:
        try:
            if not backend.speaking:
                return
        except Exception:
            return
        time.sleep(0.05)


def main() -> int:
    text = sys.stdin.read().strip("\n")
    if not text.strip():
        return EXIT_OK

    try:
        from prism import Context
    except Exception as exc:
        log(f"prism import failed: {exc}")
        return EXIT_NO_PRISM

    try:
        ctx = Context()
    except Exception as exc:
        log(f"prism context failed: {exc}")
        return EXIT_NO_PRISM

    backend, name = choose_backend(ctx)
    if backend is None:
        log("no usable backend")
        return EXIT_NO_PRISM

    apply_rate(backend)

    try:
        backend.speak(text, interrupt=True)
    except Exception as exc:
        log(f"{name}: speak failed: {exc}")
        return EXIT_NO_PRISM

    if backend.features.supports_braille:
        try:
            backend.braille(text)
        except Exception as exc:
            log(f"{name}: braille failed: {exc}")

    wait_for_speech(backend, text)
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
