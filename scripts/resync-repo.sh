#!/usr/bin/env bash
# resync-repo.sh — re-clone, re-stage, and commit the refreshed tree.
#
# ⛔ Cloning the remote rather than re-initialising a fresh repo: the first push
# created the working tree, and a naive re-init would lose the credential helper
# and force a second "initial" commit on top of the real history. Cloning keeps
# the actual commit graph and just adds a new commit to it.
set -euo pipefail
TOK="$(tr -d '\n\r' < '/mnt/c/Users/Devin Prater/AppData/Local/Temp/gh_token.txt')"
[ -n "$TOK" ] || { echo "!! no token" >&2; exit 1; }

rm -rf "$HOME/oga-work"
git clone -q "https://devinprater:${TOK}@github.com/devinprater/open-game-access.git" "$HOME/oga-work"
cd "$HOME/oga-work"
git config user.name "devinprater"
git config user.email "devinprater@live.com"
echo "== cloned: $(git log --oneline -1)"
echo "== files: $(find . -type f -not -path './.git/*' | wc -l)"
echo
echo "NOTE: staging now targets \$HOME/oga-work (the clone), not the old tree."
