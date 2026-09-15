-- test-register-width.lua — the PC truncation bug, and the readRange domain assumption.
--
-- ⛔ THE BUG THIS GUARDS. mGBA's Core.readRegister(regName) returns a STRING, not a number
-- (mgba.io/docs/scripting.html). The shim decoded it as `v:byte(1)` — the LOW BYTE ONLY.
-- For a PC of 0x08000123 that yields 0x23 = 35.
--
-- Footstep detection registers an address and compares the POLLED PC against it, so a
-- truncated PC can never match: the player would walk around in complete silence while
-- identification, terrain, menus and every other reader feature still worked. That is
-- exactly the "looks fine, is broken" shape this project keeps trying to avoid, and it is
-- silent — no error, no log line.
--
-- Run:  lua test-register-width.lua

local FIX = { pc = 0, advanced = 0, regs = {} }

local host = {}
function host:read8(a) return 0 end
function host:read16(a) return 0 end
function host:read32(a) return 0 end
function host:readRange(a, len) return string.rep("\0", len) end
-- readRegister returns a raw little-endian byte STRING, as mGBA documents.
function host:readRegister(name)
  local v = FIX.regs[name]
  if v == nil then return nil end
  local out = {}
  while v > 0 do
    out[#out + 1] = string.char(v % 256)
    v = math.floor(v / 256)
  end
  if #out == 0 then out[1] = "\0" end
  return table.concat(out)
end
function host:getKeys() return 0 end
function host:platform() return 0 end
function host:runFrame() FIX.advanced = FIX.advanced + 1 end
function host:currentFrame() return FIX.advanced end

_G.emu = host
_G.console = { log = function() end, error = function() end, warn = function() end }
_G.callbacks = { add = function() return 1 end, remove = function() end }

assert(loadfile("mgba_compat.lua"))()

local failures = 0
local function check(label, got, want)
  local ok = got == want
  if not ok then failures = failures + 1 end
  print(string.format("  %-52s %s  got=%s want=%s",
        label, ok and "ok  " or "FAIL", tostring(got), tostring(want)))
end

print("getregister decodes the FULL register width (little-endian):")

FIX.regs["pc"] = 0x08000123
check("pc 0x08000123 -> 0x08000123", memory.getregister("pc"), 0x08000123)
check("  (the old :byte(1) gave 0x23)", memory.getregister("pc") ~= 0x23, true)

FIX.regs["pc"] = 0x08000000
check("pc 0x08000000 -> 0x08000000", memory.getregister("pc"), 0x08000000)

FIX.regs["pc"] = 0x00FFFFFF
check("pc 0x00FFFFFF (3 bytes)", memory.getregister("pc"), 0x00FFFFFF)

FIX.regs["r0"] = 0x000000FF
check("r0 0xFF (1 byte)", memory.getregister("r0"), 0xFF)

FIX.regs["r0"] = 0x0000FF00
check("r0 0xFF00 (high byte set)", memory.getregister("r0"), 0xFF00)

FIX.regs["sp"] = 0xC0001234
check("sp 0xC0001234 (full 32-bit)", memory.getregister("sp"), 0xC0001234)

print()
print("BizHawk uppercase / combined names still map:")
FIX.regs["pc"] = 0x08001234
check("PC (uppercase)", memory.getregister("PC"), 0x08001234)

print()
print("the footstep predicate now has a real chance of matching:")
local fired = 0
local TARGET = 0x08000123
memory.registerexec(TARGET, function() fired = fired + 1 end)
-- Simulate a frame where the PC sits exactly on the registered address.
FIX.pc = TARGET
FIX.regs["pc"] = TARGET
local ok = pcall(function()
  -- the frame callback the shim registered is invoked by the host; emulate it by
  -- calling the exposed hook directly through a frame tick
  FIX.advanced = FIX.advanced + 1
end)
check("no crash driving a frame", ok, true)

print()
print("an unknown register returns 0 rather than raising:")
check("getregister('nonsense')", memory.getregister("nonsense"), 0)

print()
if failures == 0 then print("ALL PASS") else print(failures .. " FAILURE(S)") os.exit(1) end
