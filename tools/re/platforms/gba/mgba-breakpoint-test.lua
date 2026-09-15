-- bp-test.lua — can mGBA 0.11's REAL breakpoints replace frame-polling for exec hooks?
--
-- ⛔ THIS DECIDES WHETHER FOOTSTEP DETECTION CAN WORK AT ALL. Frame polling was measured to
-- produce 0 hits from 38 registered ROM hooks, because `runFrame()` completes a whole frame
-- before the PC can be sampled. `emu:setBreakpoint` is a real execution hook — if it fires,
-- the shim can map `memory.registerexec` onto it and the feature works.
--
-- ⛔ CORRECTING AN EARLIER MALFORMED PROBE. The previous attempt called `REAL:runFrame(REAL)`,
-- which passes an EXTRA argument (colon syntax already supplies self), and so failed with
-- "invoking failed" before any breakpoint was even installed. The control test was broken, not
-- the breakpoint API. This version uses the documented `emu:runFrame()` form.
--
-- Every step writes to the log and flushes, so a hang still leaves a usable trail.

local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/bp.txt"
local lf = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end

local function frame(n)
  for _ = 1, n do pcall(function() emu:runFrame() end) end
end

w("=== 1. control: plain emu:runFrame() x5 (no breakpoint) ===")
local before = emu:currentFrame()
frame(5)
w("   frame " .. tostring(before) .. " -> " .. tostring(emu:currentFrame()) .. "  (5 expected)")

w("=== 2. PC spread across 50 frames ===")
local seen = {}
for _ = 1, 50 do
  pcall(function() emu:runFrame() end)
  local pc = emu:readRegister("pc")
  if type(pc) == "string" then
    local v = 0
    for j = #pc, 1, -1 do v = v * 256 + pc:byte(j) end
    seen[v] = (seen[v] or 0) + 1
  end
end
local uniq, sample = 0, nil
for v, _ in pairs(seen) do uniq = uniq + 1; sample = v end
w("   distinct PC values: " .. uniq .. "  (sample " .. tostring(sample) .. ")")

w("=== 3. install a REAL breakpoint at the sampled PC ===")
local fired, cbargs = 0, nil
local ok, cbid = pcall(function()
  return emu:setBreakpoint(function(...)
    fired = fired + 1
    if cbargs == nil then cbargs = select("#", ...) end
  end, sample, -1)
end)
w("   setBreakpoint ok=" .. tostring(ok) .. " id=" .. tostring(cbid))

w("=== 4. does runFrame STILL work with a breakpoint installed? ===")
local f0 = emu:currentFrame()
local ok2, e2 = pcall(function() emu:runFrame() end)
w("   runFrame ok=" .. tostring(ok2) .. " err=" .. tostring(e2))
w("   frame advanced: " .. tostring(f0) .. " -> " .. tostring(emu:currentFrame()))

w("=== 5. run 50 frames and count fires ===")
frame(50)
w("   breakpoint fired " .. fired .. " times over 50 frames")
w("   callback received " .. tostring(cbargs) .. " argument(s)")
w("   frame now " .. tostring(emu:currentFrame()))

w("=== 6. clear it and confirm the emulator still runs ===")
if ok and cbid then pcall(function() emu:clearBreakpoint(cbid) end) end
local f1 = emu:currentFrame()
pcall(function() emu:runFrame() end)
w("   after clear: frame " .. tostring(f1) .. " -> " .. tostring(emu:currentFrame()))

w("=== 7. does setBreakpoint work on a ROM address? (the real use case) ===")
local romaddr = 0x08005844   -- ROM_RENDER_TEXT for firered/en
local fired2 = 0
local ok3, cbid2 = pcall(function()
  return emu:setBreakpoint(function() fired2 = fired2 + 1 end, romaddr, -1)
end)
w("   setBreakpoint(ROM 0x" .. string.format("%X", romaddr) .. ") ok=" .. tostring(ok3) .. " id=" .. tostring(cbid2))
local f2 = emu:currentFrame()
frame(120)
w("   fired " .. fired2 .. " times over 120 frames (frame " .. tostring(f2) .. " -> " .. tostring(emu:currentFrame()) .. ")")
if ok3 and cbid2 then pcall(function() emu:clearBreakpoint(cbid2) end) end

if lf then lf:close() end
