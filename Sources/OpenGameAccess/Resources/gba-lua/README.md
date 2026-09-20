# gba-lua — the Pokémon Access reader set, bundled unmodified.

Source: the `lua/` tree of Pokémon Access
(https://github.com/YourPalJake/pokemon-access — see the project's README for
the canonical URL), copied September 2026. These files are NOT OURS. Do not
edit them here: fixes go upstream, then this directory is re-copied. The iOS
core meets the scripts at the mGBA-shaped surface `mgba_compat.lua` expects
(see Core/gba_core.cpp) — any porting shims live on OUR side of that line.

Deliberately excluded from the upstream tree:
- *.dll (Windows screen-reader / audio bridges — iOS has no use for them)
- sounds/ (1.7 MB of WAV cues; there is no GBA audio path yet — see
  poke_read_audio, which returns 0 for Game Boy. Bundle them when the sink
  exists.)
- mgba-capability-test.lua, oga_probe_run.lua, test-exec-hook.lua,
  test-register-width.lua (diagnostics; nothing in the boot path references
  them — verified by grep before copying)

Deliberately INCLUDED even though they sound platform-specific:
- win-controls.lua (required at the top of pokemon.lua; loads inert off Windows)
- host-sim-rom.lua, oga_capture.lua (small; referenced by the reader's device
  layer in some paths — cheaper to ship than to prove unreachable)

Re-copy procedure: copy all top-level *.lua except the four diagnostics above,
plus the game/ and message/ trees whole. Then re-run the host proof
(boot Emerald to Ready) before shipping.
