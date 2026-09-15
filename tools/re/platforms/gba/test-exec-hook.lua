-- test-exec-hook.lua — verify the registerexec emulation, the highest-risk mapping.
--
-- ⛔ WHY THIS IS THE MOST IMPORTANT UNTESTED PIECE.
--
-- The readers detect FOOTSTEPS with `memory.registerexec(address, fn)` — an exec hook.
-- mGBA has NO exec hook. My shim emulates it with a per-frame poll: once a frame, read the
-- PC and, if it equals a registered address, call the reader's callback.
--
-- That substitution has NEVER fired against a real game, and it is the core "walk around and
-- hear what is ahead" feature. If it is wrong, walking is silent and the reader looks broken
-- while every other part works.
--
-- ⛔ WHAT I CAN AND CANNOT TEST HERE. There is no emulator, so I cannot prove a real game
-- triggers a footstep. What I CAN prove is the MECHANISM: that registering an address causes
-- the callback to fire when the PC matches, that nil unregisters it, that `registerwrite`
-- fires on a value change rather than every frame, and that a callback erroring does not
-- kill the session. Those are the ways the emulation would actually break.
--
-- One real concern this CANNOT settle, recorded so it is not forgotten: a frame poll can
-- MISS a routine that runs and returns inside a single frame. Whether the footstep routine
-- is long enough to be caught is a question for a real run.
--
-- Run:  lua test-exec-hook.lua

local FIX = { pc = 0, mem = {}, advanced = 0 }

local host = {}
function host:read8(a) return FIX.mem[a] or 0 end
function host:read16(a) return 0 end
function host:read32(a)
  -- read32 is used by the write hook to watch a 4-byte value.
  return (FIX.mem[a] or 0) + (FIX.mem[a+1] or 0) * 0x100
       + (FIX.mem[a+2] or 0) * 0x10000 + (FIX.mem[a+3] or 0) * 0x1000000
end
function host:readRange(a, len) return string.rep("\0", len) end
-- ⛔ readRegister reports the PC the exec hook polls. Returning the value the test sets is
-- how a "step happened" is simulated.
function host:readRegister(name)
  if name == "pc" then return FIX.pc end
  return 0
end
function host:getKeys() return 0 end
function host:platform() return 1 end
function host:runFrame() FIX.advanced = FIX.advanced + 1 end
function host:currentFrame() return FIX.advanced end

_G.emu = host
_G.console = { log = function() end, error = function() end, warn = function() end }

-- Capture the frame callback so the test can drive frames by hand.
local frame_cb = nil
_G.callbacks = {
  add = function(_, name, fn) if name == "frame" then frame_cb = fn end; return 1 end,
  remove = function() end,
}

assert(loadfile("mgba_compat.lua"))()

local failures = 0
local function check(label, got, want)
  local ok = got == want
  if not ok then failures = failures + 1 end
  print(string.format("  %-56s %s   got=%s want=%s",
        label, ok and "ok  " or "FAIL", tostring(got), tostring(want)))
end

local function frame() assert(frame_cb, "no frame callback registered") frame_cb() end

print("frame callback was registered by the shim:")
check("callbacks:add(\"frame\") was used", frame_cb ~= nil, true)

print()
print("registerexec — fires when the PC matches:")
local fired = 0
local FOOTSTEP = 0x4000
memory.registerexec(FOOTSTEP, function() fired = fired + 1 end)

FIX.pc = 0x1234; frame()            -- not the address
check("PC elsewhere -> no fire", fired, 0)
FIX.pc = FOOTSTEP; frame()          -- match
check("PC == registered addr -> fires", fired, 1)
FIX.pc = FOOTSTEP; frame()
check("fires again on the next frame", fired, 2)
FIX.pc = 0x9000; frame()
check("PC elsewhere again -> no fire", fired, 2)

print()
print("registerexec(addr, nil) unregisters — pokemon.lua:819 relies on this:")
memory.registerexec(FOOTSTEP, nil)
FIX.pc = FOOTSTEP; frame()
check("after nil, no longer fires", fired, 2)

print()
print("multiple hooks are independent:")
local a, b = 0, 0
memory.registerexec(0x100, function() a = a + 1 end)
memory.registerexec(0x200, function() b = b + 1 end)
FIX.pc = 0x100; frame()
check("A fires", a, 1)
check("B does not", b, 0)
FIX.pc = 0x200; frame()
check("B fires", b, 1)
check("A unchanged", a, 1)
memory.registerexec(0x100, nil)
memory.registerexec(0x200, nil)

print()
print("a callback that ERRORS must not kill the session:")
memory.registerexec(0x300, function() error("simulated reader bug") end)
FIX.pc = 0x300
local ok = pcall(frame)
check("frame survived a throwing callback", ok, true)
memory.registerexec(0x300, nil)

print()
print("registerwrite fires on CHANGE, not every frame:")
local writes = 0
FIX.mem[0x40000DC] = 0x00
memory.registerwrite(0x40000DC, function() writes = writes + 1 end)
frame()
check("no change -> no fire", writes, 0)
FIX.mem[0x40000DC] = 0x01
frame()
check("value changed -> fires", writes, 1)
frame()
check("unchanged again -> no fire", writes, 1)
FIX.mem[0x40000DC] = 0x02
frame()
check("changed again -> fires", writes, 2)
memory.registerwrite(0x40000DC, nil)

print()
print("hooks survive many frames without spurious firing:")
local n = 0
memory.registerexec(0x777, function() n = n + 1 end)
FIX.pc = 0x111
for _ = 1, 1000 do frame() end
check("1000 frames at an unrelated PC -> 0 fires", n, 0)
memory.registerexec(0x777, nil)

print()
if failures == 0 then print("ALL PASS") else print(failures .. " FAILURE(S)") os.exit(1) end
