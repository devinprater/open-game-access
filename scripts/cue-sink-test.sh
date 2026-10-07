#!/usr/bin/env bash
# cue-sink-test.sh — the reader's positional cues actually reach the host.
#
# ⛔ WHAT THIS PROVES THAT A GREP OR A BUILD CANNOT. `oga_audio.lua` used to be a
# recording stub with a set_sink() handoff that nothing ever called, so all 42
# `audio.play` sites were silently dropped: the pan, which IS the information, never
# left Lua. Two cheaper checks would both have passed on the broken tree:
#
#   * grepping for `oga_play_sound` — the C binding was written, and named, and the
#     registration was missing. The symbol was in the archive.
#   * building the app — it built green with the binding unreachable.
#
# So this DRIVES the shipped file: it loads the real oga_audio.lua in a real Lua 5.4
# with a stand-in host sink (the C binding's job), calls `audio.play` with the exact
# shapes the readers use, and asserts each cue arrives with its pan intact.
#
# Pass 2 is the self-test the project requires of its gates: mutate the Lua in a temp
# copy, one rule at a time, and REQUIRE the run to fail. A check that still passes
# with its rule removed was never testing that rule.
#
# No ROM, no emulator, no network: it runs in CI.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CANON="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
AUDIO="$CANON/oga_audio.lua"
CORE_C="$ROOT/Core/gba_core.cpp"
CORE_P="$ROOT/Core/pokecore.cpp"

LUA_BIN="${1:-}"
if [ -z "$LUA_BIN" ]; then
  for cand in "$HOME/src/lua-5.4.7/src/lua" lua lua5.4 lua5.3; do
    if command -v "$cand" >/dev/null 2>&1; then LUA_BIN="$(command -v "$cand")"; break; fi
    if [ -x "$cand" ]; then LUA_BIN="$cand"; break; fi
  done
fi
if [ -z "$LUA_BIN" ]; then
  echo "SKIP: no Lua interpreter found (tried lua, lua5.4, lua5.3, ~/src/lua-5.4.7)"
  exit 0
fi

[ -f "$AUDIO" ] || { echo "!! missing $AUDIO" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── the driver: counts what the host sink receives ────────────────────────────
cat > "$WORK/drive.lua" <<'LUA'
local got = {}
_G.oga_play_sound = function(path, pan, volume)
  got[#got + 1] = { path = path, pan = pan, volume = volume }
end

local chunk, err = loadfile(os.getenv("CUE_AUDIO"))
if not chunk then io.stderr:write("  FAIL: cannot load oga_audio.lua: " .. tostring(err) .. "\n"); os.exit(1) end
chunk()

local function check(name, cond)
  if not cond then io.stderr:write("  FAIL: " .. name .. "\n"); os.exit(1) end
end

-- 1. a cue built the way gb.lua builds them (the reader joins `scriptpath` itself)
audio.play("sounds\\gba\\s_grass.wav", 0, -75, 40)
check("a cue reached the host", #got == 1)
check("the path is unchanged", got[1] and got[1].path == "sounds\\gba\\s_grass.wav")
check("the pan survived (left)", got[1] and got[1].pan == -75)
check("the volume survived", got[1] and got[1].volume == 40)

-- 2. the pan IS the information: both sides must arrive as themselves
audio.play("sounds\\common\\s_wall.wav", 0, 80, 60)
check("the right-hand pan is not mirrored", got[2] and got[2].pan == 80)

-- 3. out-of-range pan clamps, never passes through or drops
audio.play("sounds\\gb\\menusel.wav", 0, 500, 30)
check("pan above range clamps to 100", got[3] and got[3].pan == 100)
audio.play("sounds\\gb\\menusel.wav", 0, -500, 30)
check("pan below range clamps to -100", got[4] and got[4].pan == -100)

-- 4. a nil pan (gb.lua:516 divides by #screen.tile_lines) must not abort the reader
audio.play("sounds\\gb\\menusel.wav", 0, nil, 30)
check("a nil pan becomes centre, not an error", got[5] and got[5].pan == 0)

-- 5. volume clamps
audio.play("sounds\\gba\\s_grass.wav", 0, 0, 999)
check("volume clamps to 100", got[6] and got[6].volume == 100)

-- 6. a nil path is dropped rather than handed to the host
local before = #got
audio.play(nil, 0, 0, 50)
check("a nil path is not delivered", #got == before)

if #got ~= 6 then io.stderr:write("  FAIL: expected 6 cues, got " .. #got .. "\n"); os.exit(1) end
os.exit(0)
LUA

pass() {  # pass <lua-file>
  CUE_AUDIO="$1" "$LUA_BIN" "$WORK/drive.lua" >/dev/null 2>&1
}

echo "cue-sink-test: the reader's cues"
echo "  lua: $LUA_BIN"

echo
echo "== the shipped file must pass"
if ! pass "$AUDIO"; then
  echo "!! the reader's cues do NOT reach the host sink" >&2
  CUE_AUDIO="$AUDIO" "$LUA_BIN" "$WORK/drive.lua" 2>&1 | grep FAIL | sed 's/^/  /' >&2
  exit 1
fi
echo "  ok: 6 cues delivered with their pans intact"

# ── the C side: the binding must exist AND be registered ─────────────────────
# The failure this catches is the one that actually happened: LuaPlaySound was
# written and compiled, but never handed to Lua, so it was inert.
# gba_core.cpp: the Lua binding, its registered name, the backend setter.
# pokecore.cpp:  the public setter and the bridge that carries cues to the app.
missing=0
check_in() {  # check_in <file> <pattern> <label>
  if ! grep -q "$2" "$1"; then
    echo "  MISSING: $3 (in ${1##*/})" >&2
    missing=1
  fi
}
check_in "$CORE_C" "static int LuaPlaySound"  "the Lua binding"
check_in "$CORE_C" "oga_play_sound"           "the binding's registered name"
check_in "$CORE_C" "gba_set_sound_callback"   "the backend setter"
check_in "$CORE_P" "poke_set_sound_callback"  "the public setter"
check_in "$CORE_P" "GbaSoundForward"          "the bridge to the app callback"
[ "$missing" -eq 0 ] || { echo "!! the C binding is incomplete" >&2; exit 1; }
echo "  ok: the C binding exists and is registered"

# ── the files the readers name must exist, or a working sink plays silence ───
for rel in "sounds/gba/s_grass.wav" "sounds/gb/s_boulder.wav" \
           "sounds/common/s_wall.wav" "sounds/gb/menusel.wav"; do
  if [ ! -f "$CANON/$rel" ]; then
    echo "  MISSING: $rel (the reader names it)" >&2
    missing=1
  fi
done
[ "$missing" -eq 0 ] || { echo "!! a named cue file is missing" >&2; exit 1; }
echo "  ok: the named cue files exist"

# ── pass 2: prove this gate CAN fail ─────────────────────────────────────────
# Each mutation removes ONE rule the check claims to guard.
echo
echo "== sabotage: each mutation must FAIL the check"

mutate() {  # mutate <sed-expr> <label>
  local expr="$1" label="$2"
  cp "$AUDIO" "$WORK/mut.lua"
  sed -i "$expr" "$WORK/mut.lua"
  if cmp -s "$AUDIO" "$WORK/mut.lua"; then
    echo "  !! sabotage '$label' changed nothing (stale pattern)" >&2
    return 2
  fi
  if pass "$WORK/mut.lua"; then
    echo "  !! NOT CAUGHT: $label" >&2
    return 1
  fi
  echo "  ok:  caught — $label"
  return 0
}

bad=0
mutate 's|^  deliver(path, pan, volume)$|  -- deliver removed|' \
       "delivery removed (the original bug)" || bad=1
mutate 's|^  deliver(path, pan, volume)$|  deliver(path, -pan, volume)|' \
       "pan mirrored (the information is wrong, not absent)" || bad=1
mutate 's|^    pan = 0$|    error("nil pan")|' \
       "nil pan raises (the reader would die mid-frame)" || bad=1
mutate 's|_G.oga_play_sound|_G.oga_play_sounds|' \
       "host global misspelled (an inert binding)" || bad=1

if [ "$bad" -ne 0 ]; then
  echo "!! sabotage verification failed: this check does not guard what it claims" >&2
  exit 1
fi

echo
echo "PASS: the reader's cues reach the host with their pan intact, and this check fails when they do not."
