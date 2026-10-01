# third-party/pokemon-ds — vendored copy (NOT a submodule)

Source: Ola's working tree at `Dropbox/Games/NDS/Lua/` (loader + `core/` +
`games/`), which he updates **in place** — there is no git upstream to track.
Do not edit these files here.

Update flow:

1. Re-copy from Dropbox: `main.lua`, `core/`, `games/` (overwrite).
2. Run `scripts/vendor-pokemon-ds.sh` — it rebuilds the single-file mobile
   bundle into `app/src/main/assets/lua/main.lua` and
   `Sources/OpenGameAccess/Resources/main.lua`, with a round-trip check.
3. Run `scripts/pokemon-ds-bundle-test.sh` — parse + boot smoke (Black
   routes and starts; Platinum/garbage fail paths speak).
4. Commit the tree + both generated outputs together.

Snapshot taken: 2026-10-01. Loader + 6 core files + `games/bw.lua`
(Black/White). The loader's GAMES table already lists Platinum/Diamond/
Pearl/HGSS/BW2 rows pointing at readers that do not exist yet — when Ola
writes one, it arrives the same way.
