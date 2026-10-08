#!/usr/bin/env bash
# registerexec-kind-test.sh — a RESET hook must not fire as a movement hook.
#
# ⛔ WHAT THIS PROVES THAT A GREP CANNOT. pokemon.lua registers init_script at the CPU's
# ENTRY VECTOR (0x100 on Game Boy, 0x8000000 on GBA) so the reader survives a soft reset.
# That is a RESET handler. Every other registration is an EFFECT predicate meant to run when
# the player moves. mGBA has no exec hook, so the shim approximates both with a per-frame poll
# -- and because it treated BOTH as movement hooks, `init_script` ran on every step: it calls
# get_game() -> load_game() and re-speaks "Ready". Measured on a real Game Boy ROM: 729
# "Ready" in a 40000-frame run, each one reloading the whole reader. Caller proven by
# traceback -- pokemon.lua:893 (inside init_script) via mgba_compat.lua:342 (pollExecEffects).
#
# A source grep would have passed the whole time: `reset_hooks` need not appear anywhere for
# the bug to exist. So this DRIVES the shipped shim in a real Lua 5.4 with a stand-in mGBA
# host, registers a callback at an entry vector and one at an effect address, simulates a
# player-movement frame, and asserts that ONLY the effect hook fired. Then it mutates the
# shim, one rule at a time, and REQUIRES each mutant to fail.
#
# No ROM, no emulator, no network: it runs in CI.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CANON="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
SHIM="$CANON/mgba_compat.lua"

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

[ -f "$SHIM" ] || { echo "!! missing $SHIM" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ── the driver: a stand-in mGBA host the shim can install itself onto ──────────────
# `emu` must be USERDATA-shaped (indexable only through a metatable) and must support the
# colon calls the shim makes. The PC is scripted so a frame can observe a chosen value.
cat > "$WORK/host.lua" <<'LUA'
-- Minimal mGBA-shaped host. Only what mgba_compat.lua touches at load + one frame.
local M = {}

local core = {}
core.__index = core
function core:platform()        return 1 end          -- 1 = GB
function core:runFrame()        return true end       -- frameadvance; PC is set by the test
function core:currentFrame()    return M.frame end
function core:readRegister(r)   if r == "pc" then return M.pc end return 0 end
function core:read8(a)          return 0 end
function core:read16(a)         return 0 end
function core:read32(a)         return 0 end
function core:readRange(a, n)   return string.rep("\0", n) end
function core:getKeys()         return 0 end
function core:setKeys(m)        end
function core:clearBreakpoint(id) end
function core:setBreakpoint()   return 1 end
function core:setRangeWatchpoint() return 1 end

M.core = setmetatable({}, core)

-- The shim resolves `emu`, `input`, `memory`, `console` as globals at load.
emu = M.core
input = setmetatable({}, { __index = function() return function() return 0 end end })
console = { log = function(_, s) end, error = function(_, s) end }
callbacks = setmetatable({}, { __index = function() return function() end end })

return M
LUA

# ── the assertions ─────────────────────────────────────────────────────────────────
cat > "$WORK/drive.lua" <<'LUA'
local M = assert(loadfile(os.getenv("HOST_LUA")))()
_G.M = M

-- Load the shipped shim. Its last line logs through REAL:platform(), which the host supports.
local chunk, err = loadfile(os.getenv("SHIM_LUA"))
if not chunk then io.stderr:write("  FAIL: cannot load mgba_compat.lua: " .. tostring(err) .. "\n"); os.exit(1) end
chunk()

local function check(name, cond)
  if not cond then io.stderr:write("  FAIL: " .. name .. "\n"); os.exit(1) end
end

check("the shim exported its BizHawk emu table", type(_G.oga_biz_emu) == "table")
check("registerexec exists", type(memory.registerexec) == "function")

-- The reader's registrations, exactly as pokemon.lua/gba.lua make them: two at the CPU entry
-- vectors (RESET handlers) and one at an ordinary address (an EFFECT/footstep hook).
local reset_fired, effect_fired = 0, 0
memory.registerexec(0x100,     function() reset_fired  = reset_fired  + 1 end)  -- GB entry vector
memory.registerexec(0x8000000, function() reset_fired  = reset_fired  + 1 end)  -- GBA entry vector
memory.registerexec(0x12345,   function() effect_fired = effect_fired + 1 end)  -- an effect hook

-- A frame in which the player MOVES. The shim's effect poll calls _G.get_player_xy; publish
-- one that reports a change from the previous frame.
local px = 3
_G.get_player_xy = function() return px, 7 end

-- Frame 1 establishes the baseline position (no movement yet, so nothing fires).
M.pc = 0x40000        -- a PC that is neither entry vector
_G.oga_biz_emu.frameadvance()
check("a baseline frame fires nothing", reset_fired == 0 and effect_fired == 0)

-- Frame 2 moves the player: the effect hook must fire, the reset hook must NOT.
px = 4
_G.oga_biz_emu.frameadvance()
check("the effect hook fired on movement", effect_fired == 1)
check("the RESET hook did NOT fire on movement (the bug)", reset_fired == 0)

-- Frame 3: no movement, nothing fires again.
_G.oga_biz_emu.frameadvance()
check("a stationary frame fires nothing further", effect_fired == 1 and reset_fired == 0)

-- And the PC sample still reaches a reset hook, which is its one legitimate trigger.
M.pc = 0x100
_G.oga_biz_emu.frameadvance()
check("the PC sample can still reach a reset hook", reset_fired == 1)

print("OK: reset hooks and effect hooks are separated (" .. reset_fired .. " reset, "
      .. effect_fired .. " effect)")
LUA

run_case() {   # $1 = shim path
  HOST_LUA="$WORK/host.lua" SHIM_LUA="$1" "$LUA_BIN" "$WORK/drive.lua"
}

echo "== pass 1: the shipped shim must pass"
if ! run_case "$SHIM"; then
  echo "!! the shipped mgba_compat.lua fails its own hook-kind contract" >&2
  exit 1
fi

echo "== pass 2: each rule, removed one at a time, must make the run FAIL"
fails=0

mutate() {   # $1 = out path, $2 = python body that edits `s`
  python3 - "$SHIM" "$1" <<PY
import io, sys
s = io.open(sys.argv[1], encoding="utf-8", errors="surrogateescape", newline="").read()
$2
io.open(sys.argv[2], "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
PY
}

# MUTATION 1: disable the entry-vector branch, so every hook is filed as an effect hook --
# the original bug, reproduced exactly.
M1="$WORK/shim-m1.lua"
mutate "$M1" '
old = "  if ENTRY_VECTORS[address] then"
assert old in s, "mutation 1 anchor missing"
s = s.replace(old, "  if false then", 1)
' || { echo "!! mutation 1 could not be applied"; exit 1; }
if run_case "$M1" >/dev/null 2>&1; then
  echo "  !! MUTATION SURVIVED: a reset hook still did not fire as an effect hook"; fails=$((fails+1))
else
  echo "  ok mutation 1 rejected (entry-vector separation is load-bearing)"
fi

# MUTATION 2: empty the entry-vector set, so nothing is treated as a reset hook.
M2="$WORK/shim-m2.lua"
mutate "$M2" '
old = "local ENTRY_VECTORS = { [0x100] = true, [0x8000000] = true }"
assert old in s, "mutation 2 anchor missing"
s = s.replace(old, "local ENTRY_VECTORS = {}", 1)
' || { echo "!! mutation 2 could not be applied"; exit 1; }
if run_case "$M2" >/dev/null 2>&1; then
  echo "  !! MUTATION SURVIVED: an empty entry-vector set still passed"; fails=$((fails+1))
else
  echo "  ok mutation 2 rejected (the entry-vector set matters)"
fi

# MUTATION 3: fire BOTH tables from the movement poll -- the pre-fix behaviour, and the exact
# shape a later edit would produce if it "simplified" the two tables back into one loop.
M3="$WORK/shim-m3.lua"
mutate "$M3" '
old = ("      for _, cb in pairs(exec_hooks) do\r\n"
       "        local okc, err = pcall(cb)\r\n")
assert old in s, "mutation 3 anchor missing"
s = s.replace(old,
              ("      for _, cb in pairs(exec_hooks) do\r\n"
               "        local okc, err = pcall(cb)\r\n"), 1)
old2 = ("      for _, cb in pairs(exec_hooks) do\r\n"
        "        local okc, err = pcall(cb)\r\n"
        "        if not okc then log(\"effect hook errored: \" .. tostring(err)) end\r\n"
        "      end\r\n")
assert old2 in s, "mutation 3 second anchor missing"
s = s.replace(old2,
              ("      for _, cb in pairs(exec_hooks) do\r\n"
               "        local okc, err = pcall(cb)\r\n"
               "        if not okc then log(\"effect hook errored: \" .. tostring(err)) end\r\n"
               "      end\r\n"
               "      for _, cb in pairs(reset_hooks) do pcall(cb) end\r\n"), 1)
' || { echo "!! mutation 3 could not be applied"; exit 1; }
if run_case "$M3" >/dev/null 2>&1; then
  echo "  !! MUTATION SURVIVED: firing reset hooks from the movement poll still passed"; fails=$((fails+1))
else
  echo "  ok mutation 3 rejected (reset hooks stay out of the movement poll)"
fi

echo
if [ "$fails" -ne 0 ]; then
  echo "!! $fails sabotage mutation(s) survived; this gate is not testing what it claims" >&2
  exit 1
fi
echo "PASS: reset hooks and effect hooks are separated, proven by 3 sabotage mutations."
