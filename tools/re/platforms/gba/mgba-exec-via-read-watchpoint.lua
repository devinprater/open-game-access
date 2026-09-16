-- exec-via-read-watchpoint.lua — can a READ watchpoint substitute for an exec breakpoint?
--
-- ⛔ THE ARCHITECTURAL FINDING THIS EXPLOITS.
--
-- From mGBA's source (src/arm/debugger/debugger.c, src/debugger/debugger.c):
--
--   * WATCHPOINTS install a MEMORY SHIM (ARMDebuggerInstallMemoryShim) directly into the ARM
--     memory layer, so they fire on every memory ACCESS no matter who drives execution.
--   * EXECUTE BREAKPOINTS do not. They are only checked from `mDebuggerRunTimeout`'s loop, via
--     `platform->checkBreakpoints`. That loop is the DEBUGGER's run loop.
--
-- A Lua script that calls `emu:runFrame()` drives frames through the core's own path, not the
-- debugger's stepping loop — so exec breakpoints are installed, counted by `hasBreakpoints`,
-- and then NEVER CHECKED. That is exactly the measured result: installed, 0 fires.
--
-- ⛔ THE WORKAROUND TO TEST: for CODE, the instruction fetch IS a memory read. So a READ
-- watchpoint over [addr, addr+4) should fire when the CPU fetches the instruction at `addr` —
-- giving us an exec hook that works under script-driven execution.
--
-- If this fires, `memory.registerexec` can be mapped onto it and footstep detection works.

local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/exec.txt"
local lf = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
local function frame(n) for _ = 1, n do pcall(function() emu:runFrame() end) end end

w("=== control: emu:runFrame() advances frames ===")
local f0 = emu:currentFrame()
frame(5)
w("   " .. tostring(f0) .. " -> " .. tostring(emu:currentFrame()))

w("=== A. READ watchpoint over a ROM address (the exec-hook substitute) ===")
-- ROM_RENDER_TEXT for firered/en. Reach gameplay first so it plausibly executes.
for i = 1, 200 do pcall(function() emu:setKeys(1) end); frame(4); pcall(function() emu:setKeys(0) end); frame(3) end
w("   reached frame " .. tostring(emu:currentFrame()))

local reads = 0
local ROMADDR = 0x08005844
local ok, id = pcall(function()
  return emu:setRangeWatchpoint(function() reads = reads + 1 end, ROMADDR, ROMADDR + 4, 1, -1)
end)
w("   setRangeWatchpoint(ROM 0x" .. string.format("%X", ROMADDR) .. ", type=READ) ok=" .. tostring(ok) .. " id=" .. tostring(id))
local before = reads
frame(120)
w("   fired " .. (reads - before) .. " times over 120 frames")
if ok and id then pcall(function() emu:clearBreakpoint(id) end) end

w("=== B. READ watchpoint over a THUMB/ARM code address we KNOW runs ===")
-- The PC sampling earlier showed ~0x0800016C executing repeatedly. Watch that.
local reads2 = 0
local HOT = 0x0800016C
local ok2, id2 = pcall(function()
  return emu:setRangeWatchpoint(function() reads2 = reads2 + 1 end, HOT, HOT + 4, 1, -1)
end)
w("   setRangeWatchpoint(0x" .. string.format("%X", HOT) .. ", READ) ok=" .. tostring(ok2) .. " id=" .. tostring(id2))
local b2 = reads2
frame(120)
w("   fired " .. (reads2 - b2) .. " times over 120 frames  <-- nonzero means exec-hook-by-read WORKS")
if ok2 and id2 then pcall(function() emu:clearBreakpoint(id2) end) end

w("=== C. does a read watchpoint break anything? ===")
local f1 = emu:currentFrame()
pcall(function() emu:runFrame() end)
w("   frame " .. tostring(f1) .. " -> " .. tostring(emu:currentFrame()))

w("=== D. can we READ the registers at that moment? (needed by cpu_set) ===")
local r1, r2
local reads3 = 0
local HOT2 = 0x0800016C
local ok3, id3 = pcall(function()
  return emu:setRangeWatchpoint(function()
    reads3 = reads3 + 1
    if reads3 == 1 then
      r1 = emu:readRegister("r1")
      r2 = emu:readRegister("r2")
    end
  end, HOT2, HOT2 + 4, 1, -1)
end)
frame(60)
w("   inside the watchpoint callback: r1=" .. tostring(r1) .. " r2=" .. tostring(r2))
w("   (reading registers from inside a watchpoint callback is what cpu_set/cpu_fast_set need)")
if ok3 and id3 then pcall(function() emu:clearBreakpoint(id3) end) end

if lf then lf:close() end
