#!/usr/bin/env bash
# side.sh — resolve a path, or run a command, on the side that actually owns it.
#
# WHY THIS EXISTS
# ---------------
# `$HOME` means different directories on each side of the Windows/WSL boundary, and
# every tool silently answers about its own. That has produced **three confident
# false results in one session**:
#
#   * `$HOME/scoop` inside WSL created a FAKE scoop tree that `mkdir -p` happily
#     made — 7 emulator cores "installed" into a directory no emulator can see;
#   * a verifier run in git-bash reported the melonDS source MISSING, because it
#     checked C:\Users\<user>/src/melonds-lua instead of /home/devin/src/melonds-lua;
#   * `/tmp` in WSL vanished between two `wsl.exe -- bash -l` calls, turning a build
#     into a no-op whose only symptom was an empty log.
#
# Documentation did not prevent any of them. This does, by making the side explicit
# at the call site instead of implicit in `$HOME`.
#
# USAGE
#   side.sh win  <path...>        # resolve under the Windows home (C:\Users\<user>)
#   side.sh wsl  <path...>        # resolve under the WSL home (/home/devin)
#   side.sh exists <win|wsl> <p>  # 0 if it exists ON THAT SIDE, else 1
#   side.sh which <name>          # where a tool lives, both sides
#   side.sh env                   # print the resolved roots (for pasting into scripts)
#
# ⛔ Never use bare `$HOME` for a cross-side path again — pick a side here.
set -uo pipefail

WIN_HOME="/c/Users/Devin Prater"
WSL_HOME_WIN="/mnt/c/Users/Devin Prater"   # how WSL reaches the Windows home
WSL_DISTRO="Ubuntu-24.04"
WSL_HOME="/home/devin"

case "${1:-}" in
  win)
    shift; printf '%s\n' "$WIN_HOME/${1:-}"
    ;;
  wsl)
    shift
    # Print BOTH the WSL-internal path and how to reach it from Windows, because
    # confusing these two is the original bug.
    printf '%s\n' "$WSL_HOME/${1:-}"
    ;;
  wsl-mount)
    shift
    printf '%s\n' "$WSL_HOME_WIN/${1:-}"
    ;;
  exists)
    side="${2:-}"; p="${3:-}"
    case "$side" in
      win) [ -e "$WIN_HOME/$p" ] && { echo "yes (win)"; exit 0; } || { echo "NO"; exit 1; } ;;
      wsl)
        if wsl.exe -d "$WSL_DISTRO" -- test -e "$WSL_HOME/$p" 2>/dev/null; then
          echo "yes (wsl)"; exit 0
        else
          echo "NO"; exit 1
        fi ;;
      *) echo "usage: side.sh exists <win|wsl> <path>" >&2; exit 2 ;;
    esac
    ;;
  which)
    shift; name="${1:-}"
    w=$(command -v "$name" 2>/dev/null || echo "-")
    l=$(wsl.exe -d "$WSL_DISTRO" -- bash -lc "command -v $name" 2>/dev/null | tr -d '\r' || true)
    printf '  %-12s windows: %s\n' "$name" "${w:--}"
    printf '  %-12s wsl    : %s\n' "" "${l:--}"
    ;;
  env)
    cat <<EOF
# resolved roots — paste into scripts instead of relying on \$HOME
WIN_HOME="$WIN_HOME"                 # git-bash \$HOME
WSL_HOME_MOUNT="$WSL_HOME_WIN"       # the Windows home AS SEEN FROM WSL
WSL_HOME="$WSL_HOME"                 # the WSL home, WSL-internal
SCOOP="$WIN_HOME/scoop"              # scoop is WINDOWS-ONLY (never \$HOME/scoop in WSL)
MELONDS_SRC_WSL="$WSL_HOME/src/melonds-lua"
LUA_SRC_WSL="$WSL_HOME/src/lua-5.4.7"
REPO_WIN="$WIN_HOME/open-game-access"
REPO_WSL="$WSL_HOME/open-game-access"
EOF
    ;;
  *)
    sed -n '2,30p' "$0" | sed 's/^# \?//'
    exit 2
    ;;
esac
