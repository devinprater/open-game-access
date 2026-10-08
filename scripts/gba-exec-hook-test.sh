#!/usr/bin/env bash
# gba-exec-hook-test.sh — the reader's register hooks are installed as REAL emulator breakpoints,
# not fired by the movement poll.
#
# ⛔ WHAT THIS PROVES THAT A GREP CANNOT. The GBA reader's text/menu hooks read CPU REGISTERS
# (memory.getregister("r1")) to find what the game was doing AT AN INSTRUCTION -- 18 of gba.lua's
# 68 hook functions and 6 of rse.lua's 19. mGBA's Lua API has no exec hook, so the shim used to
# approximate them by firing on player movement; a register read a frame late returns unrelated
# data and the reader spoke it ("49154:58718", "4", spaces). Measured fix: drive frames through
# mGBA's debugger so real breakpoints fire, and let the shim install them via oga_set_exec_hook.
# Verified on Emerald in-world: the same state and input went from that garbage to real menu items
# ("BAG", "CLOSE BAG", "Return to the field.").
#
# The failure modes this must catch are all silent:
#   * the shim stops calling oga_set_exec_hook, so every hook quietly returns to the poll;
#   * the entry-vector reset hook is routed through a breakpoint again (the "Ready" x729 bug);
#   * a hook is BOTH installed for real AND left in the movement poll, so it fires twice per frame;
#   * the frame driver stops using mDebuggerRunFrame, so breakpoints never fire at all.
#
# It drives the SHIPPED Lua against a stand-in host that records what registerexec does, then
# mutates the Lua one rule at a time and requires each mutant to FAIL. No ROM, no emulator.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CANON="$ROOT/Sources/OpenGameAccess/Resources/gba-lua"
SHIM="$CANON/mgba_compat.lua"
CORE_C="$ROOT/Core/gba_core.cpp"

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

# ── the C side must actually expose the binding the Lua relies on ─────────────────
echo "== the core exposes the real-hook API and uses the debugger driver"
grep -q 'lua_setglobal(L, "oga_set_exec_hook")' "$CORE_C" \
  || { echo "!! gba_core.cpp no longer registers oga_set_exec_hook" >&2; exit 1; }
grep -q 'mDebuggerRunFrame(&core->debugger)' "$CORE_C" \
  || { echo "!! gba_core.cpp no longer drives frames through mDebuggerRunFrame" >&2; exit 1; }
# The isPaused clear is the trap that hangs the emulator; it must be in the callback.
grep -q 'module->isPaused = false;' "$CORE_C" \
  || { echo "!! the debugger callback no longer clears isPaused -- the emulator would hang" >&2; exit 1; }
echo "   ok binding, driver, and the isPaused clear are all present"

# ── the Lua behaviour ─────────────────────────────────────────────────────────────
cat > "$WORK/drive.lua" <<'LUA'
-- Stand-in host: records what the shim asks the C side to do.
local installed, cleared = {}, {}
_G.oga_set_exec_hook = function(addr, fn) installed[addr] = fn; return true end
_G.oga_clear_exec_hook = function(addr) cleared[addr] = true; return true end
_G.oga_say = function(t, i) end
console = nil

-- Load the shipped shim. It only needs `emu` to exist for its last line.
emu = setmetatable({}, { __index = function() return function() return 0 end end })
local chunk, err = loadfile(os.getenv("SHIM_LUA"))
if not chunk then io.stderr:write("  FAIL: cannot load mgba_compat.lua: " .. tostring(err) .. "\n"); os.exit(1) end
chunk()

local function check(name, cond)
  if not cond then io.stderr:write("  FAIL: " .. name .. "\n"); os.exit(1) end
end

check("registerexec exists", type(memory.registerexec) == "function")

-- An ordinary effect hook (a menu reader that reads a register) must go to the REAL hook.
memory.registerexec(0x080C7BE0, function() return 1 end)
check("an effect hook was installed as a real breakpoint",
      installed[0x080C7BE0] ~= nil)

-- The ENTRY VECTOR must NOT become a breakpoint: it is a reset handler, and driving it per-step
-- is the "Ready" x729 bug.
memory.registerexec(0x8000000, function() return 2 end)
check("the entry-vector reset hook was NOT installed as a breakpoint",
      installed[0x8000000] == nil)

-- Unregistering must clear the real hook too, or a stale breakpoint keeps firing.
memory.registerexec(0x080C7BE0, nil)
check("unregistering cleared the real breakpoint", cleared[0x080C7BE0] == true)

print("OK: effect hooks -> real breakpoints, entry vectors excluded, unregister clears")
LUA

run_case() { SHIM_LUA="$1" "$LUA_BIN" "$WORK/drive.lua"; }

echo "== pass 1: the shipped shim must pass"
run_case "$SHIM" || { echo "!! the shipped shim fails its own hook contract" >&2; exit 1; }

echo "== pass 2: each rule, removed one at a time, must make the run FAIL"
fails=0
mutate() {  # $1=out  $2=python body
  python3 - "$SHIM" "$1" <<PY
import io, sys
s = io.open(sys.argv[1], encoding="utf-8", errors="surrogateescape", newline="").read()
$2
io.open(sys.argv[2], "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
PY
}

# MUTATION 1: stop calling the real-hook API -> everything silently falls back to the poll.
M1="$WORK/m1.lua"
mutate "$M1" '
old = "  if _G.oga_set_exec_hook then"
assert old in s, "mutation 1 anchor"
s = s.replace(old, "  if false then", 1)
' || { echo "!! mutation 1 could not be applied"; exit 1; }
if run_case "$M1" >/dev/null 2>&1; then
  echo "  !! MUTATION SURVIVED: hooks stayed on the poll"; fails=$((fails+1))
else
  echo "  ok mutation 1 rejected (the real-hook path is load-bearing)"
fi

# MUTATION 2: route the entry vector through the real-hook path too (the x729 bug).
M2="$WORK/m2.lua"
mutate "$M2" '
old = "  if ENTRY_VECTORS[address] then"
assert old in s, "mutation 2 anchor"
s = s.replace(old, "  if false then", 1)
' || { echo "!! mutation 2 could not be applied"; exit 1; }
if run_case "$M2" >/dev/null 2>&1; then
  echo "  !! MUTATION SURVIVED: an entry vector became a per-step breakpoint"; fails=$((fails+1))
else
  echo "  ok mutation 2 rejected (entry vectors stay out of the hook path)"
fi

# MUTATION 3: forget the unregister clear -> a stale breakpoint keeps firing after unregister.
M3="$WORK/m3.lua"
mutate "$M3" '
old = "    if _G.oga_clear_exec_hook then pcall(_G.oga_clear_exec_hook, address) end"
assert old in s, "mutation 3 anchor"
s = s.replace(old, "", 1)
' || { echo "!! mutation 3 could not be applied"; exit 1; }
if run_case "$M3" >/dev/null 2>&1; then
  echo "  !! MUTATION SURVIVED: unregister left the real breakpoint installed"; fails=$((fails+1))
else
  echo "  ok mutation 3 rejected (unregister clears the real hook)"
fi

echo
if [ "$fails" -ne 0 ]; then
  echo "!! $fails sabotage mutation(s) survived; this gate is not testing what it claims" >&2
  exit 1
fi
echo "PASS: register hooks are real breakpoints, entry vectors excluded, unregister clears (3 mutations)."
