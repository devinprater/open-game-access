-- reg-semantics.lua — what does mGBA 0.11's readRegister ACTUALLY return, and do real
-- breakpoints fire?
--
-- ⛔ TWO FINDINGS FROM THE PREVIOUS RUN THAT NEED PINNING DOWN.
--
-- 1. `distinct PC values: 0` — the PC decode produced NOTHING, meaning `type(pc)` was not
--    "string". The 0.10.2 docs say `readRegister(...) : string`; the **0.11 dev docs** say
--    `readRegister ( regName : string ) : wrapper`. If it is a wrapper object, then
--    `decodeRegister` passes it through unchanged and EVERY comparison against a number fails
--    — which would explain hooks never matching, independently of the frame-poll problem.
--    The readers also feed it straight into `bit.band`, so a wrapper would break that too.
--
-- 2. `setBreakpoint(ROM 0x8005844)` installed fine (id=1) but fired 0 times in 120 frames.
--    That is NOT automatically a failure: the game was still on the title screen, where
--    ROM_RENDER_TEXT may simply not run. Need an address that DEFINITELY executes.
--
-- This script answers both, writing+flushing every step so a hang still leaves a trail.

local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/reg.txt"
local lf = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
local function frame(n) for _ = 1, n do pcall(function() emu:runFrame() end) end end

w("=== A. what does readRegister return? ===")
frame(60)
local pc = emu:readRegister("pc")
w("   type = " .. type(pc))
w("   tostring = " .. tostring(pc))
w("   # (if string) = " .. tostring(type(pc) == "string" and #pc or "n/a"))
-- which coercions produce a usable NUMBER?
local coercions = {
  ["tonumber(v)"]        = function(v) return tonumber(v) end,
  ["v + 0"]              = function(v) return v + 0 end,
  ["v * 1"]              = function(v) return v * 1 end,
  ["math.floor(v)"]      = function(v) return math.floor(v) end,
  ["v | 0 (bitor)"]      = function(v) return v | 0 end,
}
for name, fn in pairs(coercions) do
  local ok, res = pcall(fn, pc)
  w(string.format("   %-18s ok=%-5s -> %s (type %s)", name, tostring(ok),
      tostring(res), type(res)))
end

w("=== B. do the same coercions work for a BITWISE op the reader needs? ===")
-- The reader does: bit.band(memory.getregister("r2"), 0x1FFFFF)
local r2 = emu:readRegister("r2")
w("   r2 type = " .. type(r2) .. " value = " .. tostring(r2))
local n2 = select(2, pcall(function() return tonumber(r2) end))
w("   tonumber(r2) = " .. tostring(n2))
if type(n2) == "number" and bit and bit.band then
  local ok, res = pcall(function() return bit.band(n2, 0x1FFFFF) end)
  w("   bit.band(tonumber(r2), 0x1FFFFF) ok=" .. tostring(ok) .. " -> " .. tostring(res))
end

w("=== C. does a REAL breakpoint fire on code that definitely executes? ===")
-- Break at the CURRENT decoded PC: if the CPU is really there, it must hit quickly.
local cur = tonumber(pc)
if type(cur) == "number" then
  local fired = 0
  local ok, cbid = pcall(function()
    return emu:setBreakpoint(function() fired = fired + 1 end, cur, -1)
  end)
  w("   setBreakpoint(0x" .. string.format("%X", cur) .. ") ok=" .. tostring(ok) .. " id=" .. tostring(cbid))
  local f0 = emu:currentFrame()
  frame(60)
  w("   fired " .. fired .. " times over 60 frames (" .. tostring(f0) .. " -> " .. tostring(emu:currentFrame()) .. ")")
  if ok and cbid then pcall(function() emu:clearBreakpoint(cbid) end) end
else
  w("   !! could not obtain a numeric PC, cannot test")
end

w("=== D. range watchpoint on a hot I/O address ===")
local wfired = 0
local okw, wid = pcall(function()
  return emu:setRangeWatchpoint(function() wfired = wfired + 1 end, 0x04000000, 0x04000010, 2, -1)
end)
w("   setRangeWatchpoint(0x04000000..0x04000010) ok=" .. tostring(okw) .. " id=" .. tostring(wid))
frame(60)
w("   fired " .. wfired .. " times over 60 frames")
if okw and wid then pcall(function() emu:clearBreakpoint(wid) end) end

w("=== E. emulator still healthy? ===")
local f2 = emu:currentFrame()
pcall(function() emu:runFrame() end)
w("   frame " .. tostring(f2) .. " -> " .. tostring(emu:currentFrame()))

if lf then lf:close() end
