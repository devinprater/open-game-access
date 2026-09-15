-- test-shim-mappings.lua — unit-test the shim's HIGH-RISK host mappings.
--
-- ⛔ WHY THIS EXISTS SEPARATELY FROM host-sim.lua. host-sim proves the reader BOOTS. This
-- proves the individual conversions are CORRECT, because the three riskiest mappings in the
-- shim all fail SILENTLY rather than erroring:
--
--   1. memory.readbyterange — mGBA returns a string; readers index a 1-based table.
--      Get this wrong and text comes back EMPTY, not broken. The reader would simply
--      announce nothing, which reads as "no text on screen" rather than a bug.
--
--   2. memory.readbytesigned / readwordsigned / readdwordsigned — mGBA returns unsigned.
--      Skip the sign conversion and -1 becomes 255. That is a VALID tile id / item id /
--      coordinate, so the reader reports a real-looking but wrong thing.
--
--   3. memory.readbyterange length handling — an off-by-one returns one byte short and
--      silently truncates the last character of every line of text.
--
-- ⛔ IMPORTANT TEST CONSTRAINT: the shim captures mGBA's methods BY VALUE at load time
-- (that is deliberate — it is what stops the overwritten methods calling themselves). So
-- every host behaviour must be configured BEFORE the shim is loaded. Reassigning
-- host.readRange afterwards does nothing, because the shim already holds the original
-- function. The first version of this test got that wrong and reported three false
-- failures.
--
-- Run:  lua test-shim-mappings.lua

-- ---- shared fixtures the fake host reads from --------------------------------
local FIX = {
  mem = {},           -- address -> byte
  range = nil,        -- function(addr, len) -> string, or nil for zero bytes
  reg_name = nil,     -- last register name requested
  advanced = 0,       -- runFrame call count
}

-- ---- the fake host, defined BEFORE the shim loads ----------------------------
local host = {}

function host:read8(a)  return FIX.mem[a] or 0 end
function host:read16(a) return (FIX.mem[a] or 0) + (FIX.mem[a + 1] or 0) * 0x100 end
function host:read32(a)
  return (FIX.mem[a] or 0) + (FIX.mem[a+1] or 0) * 0x100
       + (FIX.mem[a+2] or 0) * 0x10000 + (FIX.mem[a+3] or 0) * 0x1000000
end
function host:readRange(a, len)
  if FIX.range then return FIX.range(a, len) end
  return string.rep("\0", len)
end
function host:readRegister(name) FIX.reg_name = name; return 0 end
function host:getKeys() return 0 end
function host:platform() return 1 end
function host:runFrame() FIX.advanced = FIX.advanced + 1 end
function host:currentFrame() return 0 end

_G.emu = host
_G.console = { log = function() end, error = function() end, warn = function() end }
_G.callbacks = { add = function() return 1 end, remove = function() end }

-- ---- load the shim (captures the methods above) ------------------------------
assert(loadfile("mgba_compat.lua"))()

-- ---- checks -----------------------------------------------------------------
local failures = 0
local function check(label, got, want)
  local ok = got == want
  if not ok then failures = failures + 1 end
  print(string.format("  %-52s %s   got=%s want=%s",
        label, ok and "ok  " or "FAIL", tostring(got), tostring(want)))
end

print("platform:")
check("emu.platform() passes the NUMBER through", emu.platform(), 1)

print()
print("readbyterange — MUST return a 1-based table matching readRange's string:")

-- The literal the host will serve. ASCII so the expected values are obvious.
FIX.range = function(a, len) return ("ABCDE"):sub(1, len) end

local t = memory.readbyterange(0x1000, 5)
check("type is table (not string)", type(t), "table")
check("no [0] key (1-based, like BizHawk)", t[0], nil)
check("t[1] == 'A' (0x41)", t[1], 0x41)
check("t[2] == 'B'", t[2], 0x42)
check("t[3] == 'C'", t[3], 0x43)
check("t[4] == 'D'", t[4], 0x44)
check("t[5] == 'E' (LAST byte present)", t[5], 0x45)
check("t[6] == nil (no overshoot)", t[6], nil)
check("#t == 5 (length exact, no truncation)", #t, 5)

-- The gb.lua access pattern, verbatim: `for i = 1, 360, 20 do ... raw_text[i+j] ...`
FIX.range = function(a, len)
  local s = {}
  for i = 1, len do s[i] = string.char((i - 1) % 256) end
  return table.concat(s)
end
local rt = memory.readbyterange(0x2000, 360)
check("length 360 as gb.lua requests", #rt, 360)
check("rt[1] == 0 (row 1, j=0)", rt[1], 0)
check("rt[21] == 20 (row 2 first byte, i=21 j=0)", rt[21], 20)
check("rt[339] reachable (SCROLL_INDICATOR_POSITION)", type(rt[339]), "number")
check("rt[361] is nil (no wrap past end)", rt[361], nil)

-- A high byte must survive as a high byte: 0xED is a control char the reader looks for.
FIX.range = function(a, len) return string.char(0xED, 0xEE, 0xFF, 0x00, 0x7F) end
local hb = memory.readbyterange(0x3000, 5)
check("rt high byte 0xED preserved (menu marker)", hb[1], 0xED)
check("rt high byte 0xEE preserved (scroll marker)", hb[2], 0xEE)
check("rt byte 0xFF == 255 (not sign-extended)", hb[3], 255)
check("rt byte 0x00 == 0", hb[4], 0)
check("rt byte 0x7F == 127", hb[5], 127)

print()
print("signed reads — mGBA returns unsigned, readers need signed:")
FIX.mem[0x10] = 0xFF
FIX.mem[0x11] = 0xFF
FIX.mem[0x12] = 0xFF
FIX.mem[0x13] = 0xFF
FIX.mem[0x20] = 0x00
FIX.mem[0x30] = 0x7F
FIX.mem[0x31] = 0x80
check("readbyte 0xFF == 255 (unsigned)", memory.readbyte(0x10), 255)
check("readbyteunsigned 0xFF == 255", memory.readbyteunsigned(0x10), 255)
check("readbytesigned 0xFF == -1 (SIGN EXTENDED)", memory.readbytesigned(0x10), -1)
check("readwordsigned 0xFFFF == -1", memory.readwordsigned(0x10), -1)
check("readdwordsigned 0xFFFFFFFF == -1", memory.readdwordsigned(0x10), -1)
check("readbytesigned 0x00 == 0", memory.readbytesigned(0x20), 0)
check("readbytesigned 0x7F == 127 (positive unaffected)", memory.readbytesigned(0x30), 127)
check("readbytesigned 0x80 == -128 (boundary)", memory.readbytesigned(0x31), -128)

print()
print("register name mapping — readers use uppercase and combined pairs:")
local seen = {}
FIX.reg_name = nil
-- Wrap by re-reading FIX each time; getregister writes FIX.reg_name.
local function regcase(n) FIX.reg_name = nil; memory.getregister(n); return FIX.reg_name end
check("A  -> a",  regcase("A"),  "a")
check("BC -> bc", regcase("BC"), "bc")
check("HL -> hl", regcase("HL"), "hl")
check("PC -> pc", regcase("PC"), "pc")
check("SP -> sp", regcase("SP"), "sp")
check("a  -> a (already lower)", regcase("a"), "a")

print()
print("frameadvance routes to the host loop:")
local before = FIX.advanced
emu.frameadvance(); emu.frameadvance()
check("emu.frameadvance() advanced the host twice", FIX.advanced - before, 2)

print()
print("readword composes little-endian:")
FIX.mem[0x40] = 0x34
FIX.mem[0x41] = 0x12
check("readword 0x1234 from bytes 34,12", memory.readword(0x40), 0x1234)

print()
if failures == 0 then print("ALL PASS") else print(failures .. " FAILURE(S)") os.exit(1) end
