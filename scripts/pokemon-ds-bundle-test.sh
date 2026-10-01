#!/usr/bin/env bash
# pokemon-ds-bundle-test.sh — verify the generated mobile DS bundle.
#
# The bundle (scripts/vendor-pokemon-ds.sh output) must, with the REAL
# bizhawk_compat shim in front of it:
#   1. parse (luac -p when available),
#   2. route a Black ROM header to games/bw.lua, which must COMPILE and START
#      (reach its first emu.frameadvance),
#   3. answer a known-but-unwritten game (Platinum) with "not supported yet",
#   4. answer garbage with "could not identify".
#
# This runs the exact chunk the apps ship (shim + bundle) under desktop Lua
# with stub core APIs — no ROM, no emulator, seconds. It CANNOT prove the
# reader is right on a live game (Ola proves that in EmuHawk); it proves the
# bundling is faithful and the loader routes.
set -uo pipefail
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # Native lua/python cannot read MSYS /c/... paths: use the C:/... form.
    ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -W)"
    ;;
  *)
    ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    ;;
esac
cd "$ROOT" || exit 1

BUNDLE_APP="app/src/main/assets/lua/main.lua"
BUNDLE_IOS="Sources/OpenGameAccess/Resources/main.lua"
SHIM="Sources/OpenGameAccess/Resources/bizhawk_compat.lua"

echo "== rebuilding the bundle"
bash "$ROOT/scripts/vendor-pokemon-ds.sh" || exit 1

if cmp -s "$BUNDLE_APP" "$BUNDLE_IOS"; then
    echo "ok: both app copies identical"
else
    echo "!! app and iOS bundles differ" >&2
    exit 1
fi

if command -v luac >/dev/null 2>&1; then
    luac -p "$BUNDLE_IOS" && echo "ok: bundle parses"
else
    echo "-- luac not on PATH here; parse check skipped (CI phones parse at load)"
fi

# --- smoke driver (thrown away after the run; under $ROOT so the path is ---
# --- native on every platform — MSYS /c/..., WSL and macOS alike) ----------
WORK="$ROOT/.ds-smoke-$$"
rm -rf "$WORK"; mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT
DRIVER="$WORK/smoke.lua"
cat > "$DRIVER" <<'LUAEOF'
-- args: 1=shim path 2=bundle path 3=mode (black|platinum|garbage)
local function readfile(p)
    local f = assert(io.open(p, "rb"))
    local s = f:read("*a"); f:close()
    return s
end
local shim = readfile(arg[1])
local bundle = readfile(arg[2])
local mode = arg[3]

local SPOKEN, LOGGED = {}, {}
-- Core-provided APIs the shim builds on (stubs: Black header / broken ROM).
memory = {}
local rom = { IRBO = { 0x49, 0x52, 0x42, 0x4F } }
function memory.read_u8(addr, domain)
    if domain == "ROM" then
        if mode == "black" then
            for i = 0, 3 do
                if addr == 0x0C + i then return rom.IRBO[i + 1] end
            end
            return 0
        end
        error("no ROM domain in this stub")
    end
    return 0
end
function memory.read_u16_le() return 0 end
function memory.read_u32_le() return 0 end
function memory.write_u8() end
function memory.read_bytes_as_array(a, n)
    local t = {}
    for i = 1, n do t[i] = 0 end
    return t
end

input = {}
function input.HeldKeys() return {} end

if mode ~= "black" then
    gameinfo = {}
    function gameinfo.getromname()
        if mode == "platinum" then return "Pokemon - Platinum Version (USA)" end
        return "Black Sigil"
    end
end

console = {}
function console.log(s) LOGGED[#LOGGED + 1] = tostring(s) end

speech = {}
function speech.say(text, interrupt)
    SPOKEN[#SPOKEN + 1] = tostring(text)
end
function speech.stop() end

local chunk = assert(load(shim .. "\n" .. bundle, "@mobile-chunk"))
local co = coroutine.create(chunk)
local ok, err = coroutine.resume(co)
-- The reader never returns: Black must still be suspended (yielded at its
-- first frameadvance); the fail paths return normally after speaking.
if mode == "black" then
    assert(ok and coroutine.status(co) == "suspended",
           "black: expected the reader to start and yield, got: " .. tostring(err))
    print("ok: black header routes to games/bw.lua and the reader starts")
    local saw_loader = false
    for _, l in ipairs(LOGGED) do
        if l:find("Pokemon Black", 1, true) and l:find("games/bw.lua", 1, true) then
            saw_loader = true
        end
    end
    assert(saw_loader, "black: loader did not log the handover")
    print("ok: loader logged the handover")
else
    assert(ok and coroutine.status(co) == "dead",
           mode .. ": expected the loader to return, got: " .. tostring(err))
    local want = mode == "platinum" and "not supported yet" or "Could not identify"
    local found = false
    for _, s in ipairs(SPOKEN) do
        if s:find(want, 1, true) then found = true end
    end
    assert(found, mode .. ": expected speech containing '" .. want .. "'")
    print("ok: " .. mode .. " speaks '" .. want .. "'")
end
print("SMOKE-PASS " .. mode)
LUAEOF

if ! command -v lua >/dev/null 2>&1; then
    echo "-- no desktop lua here; runtime smoke skipped (parse + round-trip only)"
    exit 0
fi

# The loader under test must be the COMMITTED bundle, not a stale copy.
for mode in black platinum garbage; do
    echo "== smoke: $mode"
    lua "$DRIVER" "$ROOT/$SHIM" "$ROOT/$BUNDLE_IOS" "$mode" || exit 1
done
echo "ALL DS BUNDLE TESTS PASSED"
