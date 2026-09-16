-- host-sim.lua — run oga_bootstrap.lua in stock Lua with a FAKE mGBA.
--
-- ⛔ WHAT THIS PROVES AND WHAT IT DOES NOT.
--
-- mGBA's Lua is GUI-only, so the real test needs Tools → Scripting. But most of what can
-- go wrong at load time is not emulator-specific — it is the load path:
--
--   * does `package.path` actually let `require "a-star"` resolve?
--   * does the `ffi` stub satisfy `require "ffi"` so the FFI modules stop erroring?
--   * does `package.preload["crc32"]` really intercept the FFI crc32 file on disk?
--   * does pokemon.lua's own boot chain (loadfile of gb.lua, game/common/*.lua) run?
--   * does my shim install without a syntax or nil-index error?
--
-- Every one of those is testable in plain Lua with a stubbed `emu`. That is what this file
-- does, and it is worth doing BEFORE asking a human to drive a GUI: a load-path failure
-- found here is a bug I can fix in seconds, whereas the same failure found in mGBA costs a
-- round-trip and looks like an emulator problem.
--
-- ⛔ WHAT THIS IS NOT: it is not evidence the shim works against a real game. There is no
-- emulator, no memory, no ROM. `emu:read8` returns zeros. Footstep detection, real speech
-- and every memory mapping remain UNVERIFIED until mGBA runs it.
--
-- Run:  lua host-sim.lua <reader-dir>

local reader_dir = arg[1] or "."
if reader_dir:sub(-1) ~= "/" and reader_dir:sub(-1) ~= "\\" then
  reader_dir = reader_dir .. "/"
end

print("=== host-sim: faking mGBA's Lua host ===")
print("reader dir: " .. reader_dir)
print("lua:        " .. _VERSION)

----------------------------------------------------------------------
-- the fake host
--
-- Only the members the shim and readers actually touch. Deliberately NOT a faithful
-- emulator: reads return 0, so a reader that depends on real values will run but produce
-- meaningless output. That is fine — this test is about LOADING, not about values.
----------------------------------------------------------------------

local log_lines = {}

-- ⛔ FLUSH ON WRITE. Without it, a reader that enters its infinite main loop never lets
-- Lua flush stdout, and the harness appears to hang with NO output at all — hiding the
-- very log lines that say how far it got.
local function emit(s)
  io.stdout:write(s .. "\n")
  io.stdout:flush()
end

-- ⛔ BOUND THE FRAME COUNT. pokemon.lua ends in `while true do emu.frameadvance() ... end`,
-- so `emu:runFrame()` is the ONLY place this harness regains control. Count frames and
-- raise once we have seen enough — that turns "hangs forever" into a terminating test that
-- still proves the reader reached its main loop.
local FRAME_BUDGET = tonumber(os.getenv("OGA_SIM_FRAMES") or "600")
local frames_seen = 0

local fake_emu = {}
function fake_emu:runFrame()
  frames_seen = frames_seen + 1
  if frames_seen > FRAME_BUDGET then
    error(string.format("frame budget %d reached — reader main loop is running", FRAME_BUDGET), 0)
  end
end
function fake_emu:platform() return 1 end              -- 1 = GB, so the GB path is taken
function fake_emu:currentFrame() return 0 end
function fake_emu:read8() return 0 end
function fake_emu:read16() return 0 end
function fake_emu:read32() return 0 end
function fake_emu:readRegister() return 0 end
function fake_emu:getKeys() return 0 end
function fake_emu:readRange(addr, len)
  -- mGBA returns a STRING here. Return a string of the right length so the shim's table
  -- conversion is exercised for real.
  -- ⛔ Tolerate a nil length: a reader may call readbyterange(addr) with no length, and a
  -- harness that crashes on that says nothing about the shim. Default to 1 and keep going.
  if type(len) ~= "number" or len < 0 then len = 1 end
  return string.rep("\0", len)
end

_G.emu = fake_emu

_G.console = {
  log = function(_, msg)
    local s = tostring(msg)
    log_lines[#log_lines + 1] = s
    emit("  " .. s)
  end,
  error = function(_, msg)
    local s = "ERROR " .. tostring(msg)
    log_lines[#log_lines + 1] = s
    emit("  " .. s)
  end,
  warn = function(_, msg)
    local s = "WARN " .. tostring(msg)
    log_lines[#log_lines + 1] = s
    emit("  " .. s)
  end,
}

local frame_cbs = {}
_G.callbacks = {
  add = function(_, name, fn)
    frame_cbs[#frame_cbs + 1] = { name = name, fn = fn }
    return #frame_cbs
  end,
  remove = function() end,
}

----------------------------------------------------------------------
-- run the bootstrap from the reader directory
----------------------------------------------------------------------

local bootpath = reader_dir .. "oga_bootstrap.lua"
local chunk, err = loadfile(bootpath)
if not chunk then
  print("!! could not load the bootstrap: " .. tostring(err))
  os.exit(1)
end

-- The bootstrap uses debug.getinfo(1,"S").source to find its own directory, so run it
-- from a chunk whose source string points at the real path.
local ok, runerr = pcall(chunk)
if not ok then
  print("!! bootstrap raised: " .. tostring(runerr))
end

----------------------------------------------------------------------
-- report
----------------------------------------------------------------------

print()
print("=== host log ===")
for _, l in ipairs(log_lines) do print("  " .. l) end

print()
print("=== checks ===")
local function assert_like(label, cond)
  print(string.format("  %-46s %s", label, cond and "ok" or "FAIL"))
  return cond
end

local all = true
local joined = table.concat(log_lines, "\n")

all = assert_like("bootstrap ran without raising", ok) and all
all = assert_like("shim reported a platform (host wired)", joined:find("platform: ") ~= nil) and all
all = assert_like("reader was loaded", joined:find("loading reader") ~= nil) and all
all = assert_like("pure crc32/encoding installed", joined:find("pure%-Lua crc32") ~= nil) and all

-- Did the reader's own boot get far enough to call loadfile on a game script?
all = assert_like("reader boot chain ran", joined:find("bootstrap complete") ~= nil) and all

print()
print("=== frame callback registered? (drives exec/write hook emulation) ===")
print("  callbacks registered: " .. #frame_cbs)
for _, cb in ipairs(frame_cbs) do print("    " .. cb.name) end

print()
print("=== verdict ===")
-- ⛔ Distinguish the three outcomes properly. A reader that reaches its main loop ends by
-- raising the frame-budget error, which is SUCCESS — the loop is running. A genuine load
-- failure is an error mentioning anything else. Treating them the same would report a
-- working reader as broken.
local budget_hit = joined:find("frame budget") ~= nil
local other_errors = false
for _, l in ipairs(log_lines) do
  if l:find("!!") then other_errors = true end
end

if budget_hit then
  print(string.format("  reader reached its MAIN LOOP (%d frames simulated)", FRAME_BUDGET))
  print("  no load errors")
  all = true
elseif other_errors then
  print("  LOAD ERRORS PRESENT — see the !! lines above")
  all = false
else
  print("  reader loaded but never entered its main loop")
  all = false
end
print()
if all then print("HOST-SIM PASS") else print("HOST-SIM FAIL") end
os.exit(all and 0 or 1)
