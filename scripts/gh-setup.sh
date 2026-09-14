#!/usr/bin/env bash
# gh-setup.sh — install the GitHub token as a git credential inside WSL.
#
# Writing ~/.git-credentials (mode 600) rather than embedding the token in the
# remote URL: a URL-embedded token is visible in `git remote -v` and in every
# error message, and one stray `git remote add` would leak it.
set -euo pipefail
SRC="/mnt/c/Users/Devin Prater/AppData/Local/Temp/gh_token.txt"
TOK="$(tr -d '\n\r' < "$SRC")"
[ -n "$TOK" ] || { echo "!! empty token from $SRC" >&2; exit 1; }
echo "token length: ${#TOK}"

cd "$HOME/open-game-access"
git remote remove origin 2>/dev/null || true
git config credential.helper store
printf 'https://devinprater:%s@github.com\n' "$TOK" > "$HOME/.git-credentials"
chmod 600 "$HOME/.git-credentials"
git remote add origin https://github.com/devinprater/open-game-access.git

echo "== pushing =="
git push -u origin HEAD:main 2>&1 | tail -15
echo
echo "== remote state =="
git ls-remote --heads origin 2>&1 | head -5
