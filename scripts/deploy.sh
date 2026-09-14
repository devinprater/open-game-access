#!/usr/bin/env bash
# deploy.sh — sync to WSL, build the core, then build + install + launch the app
# on the attached iPhone via xtool.
#
# The device link is the Windows Apple-mobile-device mux forwarded into WSL;
# see the xtool-ios-on-wsl skill for why usbipd cannot be used here.
set -euo pipefail

WIN_SRC="/mnt/c/Users/Devin Prater/pokemon-access-ios"
WSL_DST="$HOME/pokemon-access-ios"

rsync -a --delete \
  --exclude '.build/' --exclude 'xtool/' \
  --exclude 'Vendor/obj/' --exclude 'Vendor/*.a' \
  "$WIN_SRC/" "$WSL_DST/"

cd "$WSL_DST"

# The core archive is the expensive part; only rebuild when it is missing.
if [ ! -f Vendor/libpokecore.a ]; then
  echo "== core archive missing, building =="
  bash scripts/build-core.sh
fi

# SwiftPM resolves linker paths against the process working directory, which it
# does not pin to the package root, so the archive is handed over absolutely.
export POKECORE_LIB="$WSL_DST/Vendor/libpokecore.a"

HOSTIP=$(ip route list default | awk '{print $3}')
export USBMUXD_SOCKET_ADDRESS="$HOSTIP:27015"
if [ -S /var/run/usbmuxd ]; then
  sudo systemctl stop usbmuxd 2>/dev/null || true
  sudo rm -f /var/run/usbmuxd
fi
if ! timeout 8 bash -c "cat < /dev/null > /dev/tcp/$HOSTIP/27015" 2>/dev/null; then
  echo "!! cannot reach the device mux at $HOSTIP:27015" >&2
  echo "   run the elevated setup-usbmux-forward helper on Windows first" >&2
  exit 1
fi

xtool dev "$@"
