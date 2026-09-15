-- mgba_compat.lua — host-API shim: BizHawk-style reader calls → mGBA's Lua API.
--
-- ⛔ WHAT THIS IS. The Pokémon Access GB/GBC/GBA reader set (166 Lua files, 688 KB) is
-- written against BizHawk's Lua API. mGBA's API is object-oriented and different. This
-- shim presents the BizHawk surface the readers actually use and forwards to mGBA. The
-- readers are NOT edited — if a reader needs a change, that is evidence this shim is
-- incomplete, not a licence to modify the scripts.
--
-- ⛔ THE SURFACE IS MEASURED, NOT GUESSED. tools/re/platforms/gba/grep-reader-api.py
-- enumerates every host call in all 166 files. The result is 16 distinct functions:
--
--   emu.     frameadvance (2)   platform (1)
--   memory.  readbyte (161)          readword (80)          readdword (76)
--            getregister (72)        gbromreadbyte (48)     readbyteunsigned (6)
--            registerexec (6)        readbyterange (6)      gbromreadword (2)
--            readbytesigned (2)      registerwrite (1)      readdwordsigned (1)
--   input.   read (1)
--
-- ⛔ THREE REAL MISMATCHES, each handled explicitly below:
--
--   1. readbyterange must return a 1-BASED TABLE of byte values, not a string. The
--      readers do `raw_text[i+j]` with i starting at 1 (gb.lua:47-59). mGBA's
--      emu:readRange() returns a STRING, and indexing a Lua string with [1] yields nil —
--      so a naive passthrough would silently produce empty text rather than an error.
--      This is the single most dangerous mapping in the shim.
--
--   2. registerexec/registerwrite have NO mGBA equivalent. mGBA offers no exec hook, and
--      these are not decorative: registerexec is how the readers detect FOOTSTEPS (the
--      primary accessibility feature), and registerwrite is used once for GBA DMA-into-
--      VRAM detection. Both are emulated by a per-frame poll.
--
--   3. getregister needs a name mapping. BizHawk uses uppercase and may ask for combined
--      pairs (BC/DE/HL/AF); mGBA's docs list lowercase names plus those pairs.
--
-- Install: load this file in mGBA's Tools → Scripting window, then load the reader's
-- own entry point (pokemon.lua / gb.lua / gba.lua) via the reader's normal boot path.

----------------------------------------------------------------------
-- host access, with a clear failure if the shim runs on the wrong host
----------------------------------------------------------------------

local HOST = emu
if HOST == nil then
  -- Not fatal in a way that should abort: report and continue so the message is visible.
  if console and console.error then
    console:error("[oga-shim] no `emu' object — this shim must run inside mGBA with a game loaded")
  end
  return
end

local function log(msg)
  if console and console.log then console:log("[oga-shim] " .. tostring(msg)) end
end

----------------------------------------------------------------------
-- signedness helpers
--
-- BizHawk exposes separate signed/unsigned reads because Lua has no integer types.
-- mGBA returns unsigned values, so the signed variants must be converted by hand.
-- Getting this wrong turns a -1 tile id into 255, which reads as a valid but WRONG tile.
----------------------------------------------------------------------

local function sign8(v)  v = v % 0x100;        return v >= 0x80 and v - 0x100 or v end
local function sign16(v) v = v % 0x10000;      return v >= 0x8000 and v - 0x10000 or v end
local function sign32(v) v = v % 0x100000000;  return v >= 0x80000000 and v - 0x100000000 or v end

----------------------------------------------------------------------
-- register name mapping: BizHawk names → mGBA names
--
-- The readers use 72 register reads, so a bad mapping here is high-impact. Both
-- uppercase and lowercase forms are accepted because a reader may use either.
----------------------------------------------------------------------

local REG_MAP = {
  -- GB (and the GBA's ARM registers share letters where they overlap)
  A="a", B="b", C="c", D="d", E="e", H="h", L="l", F="f",
  BC="bc", DE="de", HL="hl", AF="af", PC="pc", SP="sp",
  -- GBA ARM registers
  R0="r0", R1="r1", R2="r2", R3="r3", R4="r4", R5="r5", R6="r6", R7="r7",
  R8="r8", R9="r9", R10="r10", R11="r11", R12="r12", R13="r13", R14="r14", R15="r15",
  IP="ip", LR="lr", CPSR="cpsr",
}

local function mapReg(name)
  if type(name) ~= "string" then return nil end
  return REG_MAP[name] or REG_MAP[name:upper()] or name:lower()
end

----------------------------------------------------------------------
-- emu
----------------------------------------------------------------------

emu = emu or {}

-- ⛔ CAPTURE mGBA'S METHODS BY VALUE, NOT BY TABLE REFERENCE.
--
-- `emu` IS mGBA's CoreAdapter — the host object itself. So keeping a reference to it and
-- then overwriting `emu.platform` / `emu.frameadvance` / `emu.read8` below makes those
-- replacements call THEMSELVES:
--
--     local mgba_emu = HOST            -- HOST == emu, the same table
--     emu.platform = function() return HOST_platform(HOST_for_self) end
--                                  -- ^ this is now emu.platform again -> infinite recursion
--
-- The failure is `stack overflow` pointing at the first overwritten method, which reads
-- like a shim bug in the wrong function. So grab the ORIGINAL functions into locals first,
-- before anything is replaced. (Found by host-sim.lua, not by reading the code.)
local HOST_for_self = HOST   -- mGBA's methods are colon-bound; they need `self`.
local HOST_platform  = HOST.platform
local HOST_frameadv   = HOST.runFrame
local HOST_frame      = HOST.currentFrame
local HOST_read8      = HOST.read8
local HOST_read16     = HOST.read16
local HOST_read32     = HOST.read32
local HOST_readRange  = HOST.readRange
local HOST_readReg    = HOST.readRegister
local HOST_getKeys    = HOST.getKeys

emu.frameadvance = function()
  HOST_frameadv(HOST_for_self)
end

-- BizHawk's emu.platform() returns a NUMERIC enum, and the readers compare it to numbers.
-- pokemon.lua:897 does exactly:
--
--     function get_device()
--       local id = emu.platform()
--       if id == 0 then return "gba"
--       elseif id == 1 then return "gb" end
--       return nil
--     end
--
-- ⛔ RETURNING A STRING HERE BREAKS THE READER. get_device() then returns nil, `device`
-- stays nil, and the failure surfaces far away as
-- `attempt to concatenate a nil value (global 'device')` at the boot line that builds a
-- script path. Returning the raw mGBA number is both simpler and correct, because mGBA
-- uses the SAME values (0 = GBA, 1 = GB).
emu.platform = function()
  return HOST_platform(HOST_for_self)
end

emu.framecount = function()
  return HOST_frame(HOST_for_self)
end

----------------------------------------------------------------------
-- memory
----------------------------------------------------------------------

memory = memory or {}

memory.readbyte = function(a) return HOST_read8(HOST_for_self, a) end
memory.readword = function(a) return HOST_read16(HOST_for_self, a) end
memory.readdword = function(a) return HOST_read32(HOST_for_self, a) end

memory.readbyteunsigned = function(a) return HOST_read8(HOST_for_self, a) end
memory.readbytesigned   = function(a) return sign8(HOST_read8(HOST_for_self, a)) end
memory.readwordsigned   = function(a) return sign16(HOST_read16(HOST_for_self, a)) end
memory.readdwordsigned  = function(a) return sign32(HOST_read32(HOST_for_self, a)) end

-- ⛔ THE CRITICAL MAPPING. See the header note: readers index this as a 1-based table.
-- mGBA's readRange returns a string, which yields nil when indexed numerically — a
-- silent empty-text bug rather than an error. Convert to a table, 1-based, to match
-- BizHawk exactly.
memory.readbyterange = function(addr, length)
  local raw = HOST_readRange(HOST_for_self, addr, length)
  if type(raw) ~= "string" then
    -- Some mGBA builds may already return a table; accept it if it is 1-based.
    if type(raw) == "table" then
      log("readbyterange returned a table (not a string) — check 1-based assumption")
      return raw
    end
    log("readRange returned " .. type(raw) .. " — expected string")
    return {}
  end
  local out = {}
  for i = 1, length do
    -- string.byte with an explicit index is 1-based, which is what the readers want.
    out[i] = raw:byte(i) or 0
  end
  return out
end

-- GB ROM reads. On a banked cartridge a plain read8 at a 0x0000-0x7FFF address returns
-- whatever bank is currently mapped, which is what the readers want (they read the bank
-- byte from HRAM separately and account for it themselves).
memory.gbromreadbyte = function(a)
  local ok, v = pcall(function() return HOST_read8(HOST_for_self, a) end)
  if ok and v then return v end
  log("gbromreadbyte failed at 0x" .. string.format("%x", a))
  return 0
end

memory.gbromreadword = function(a)
  local lo = memory.gbromreadbyte(a)
  local hi = memory.gbromreadbyte(a + 1)
  return lo + hi * 0x100
end

memory.getregister = function(name)
  local mapped = mapReg(name)
  if mapped == nil then return 0 end
  local ok, v = pcall(function() return HOST_readReg(HOST_for_self, mapped) end)
  if not ok or v == nil then
    log("getregister failed for " .. tostring(name))
    return 0
  end
  if type(v) == "string" then
    -- ⛔ mGBA's readRegister returns a STRING, and the bytes are little-endian.
    -- This previously did `v:byte(1)`, which takes ONLY the low byte — for a PC of
    -- 0x08000123 that yields 0x23 (35). Footstep detection compares the PC against a
    -- registered address, so a truncated PC NEVER matches and walking would be silent
    -- while every other feature worked. Decode the full width instead.
    local n = #v
    if n == 0 then return 0 end
    local val = 0
    for i = n, 1, -1 do          -- little-endian: last byte is most significant
      val = val * 0x100 + v:byte(i)
    end
    return val
  end
  return v
end

----------------------------------------------------------------------
-- registerexec / registerwrite — emulated by per-frame polling
--
-- ⛔ mGBA HAS NO EXEC HOOK OR WRITE HOOK. These are implemented as frame callbacks that
-- re-evaluate the original predicate. This is an approximation and it must be verified
-- rather than assumed:
--
--   - A routine that runs and returns WITHIN one frame can be missed by a frame poll.
--     Footstep detection is exactly this shape, and footsteps are the primary
--     accessibility feature, so this is the highest-risk part of the shim.
--   - If footsteps prove unreliable, the fallback is to poll the observable EFFECT
--     (the ROM bank byte plus a position change) rather than the PC.
--
-- Do not add that fallback pre-emptively: establish whether it is needed first.
----------------------------------------------------------------------

local exec_hooks = {}   -- address -> callback
local write_hooks = {}  -- address -> { cb = fn, last = value }

memory.registerexec = function(address, fn)
  if fn == nil then
    exec_hooks[address] = nil
    return
  end
  exec_hooks[address] = fn
end

memory.registerwrite = function(address, fn)
  if fn == nil then
    write_hooks[address] = nil
    return
  end
  write_hooks[address] = { cb = fn, last = HOST_read32(HOST_for_self, address) }
end

-- One frame callback drives every emulated hook.
callbacks:add("frame", function()
  -- Exec hooks: poll the PC.
  if next(exec_hooks) ~= nil then
    local pc_ok, pc = pcall(function()
      local v = HOST_readReg(HOST_for_self, "pc")
      if type(v) == "string" then return v:byte(1) or 0 end
      return v
    end)
    if pc_ok and pc then
      local cb = exec_hooks[pc]
      if cb then
        -- A reader callback that errors must not kill the emulator session.
        local ok, err = pcall(cb)
        if not ok then log("exec hook at " .. tostring(pc) .. " errored: " .. tostring(err)) end
      end
    end
  end

  -- Write hooks: poll the watched word.
  for addr, h in pairs(write_hooks) do
    local ok, now = pcall(function() return HOST_read32(HOST_for_self, addr) end)
    if ok and now ~= h.last then
      h.last = now
      local okc, err = pcall(h.cb)
      if not okc then log("write hook at " .. tostring(addr) .. " errored: " .. tostring(err)) end
    end
  end
end)

----------------------------------------------------------------------
-- input
--
-- BizHawk's input.read() returns the current pad state. mGBA exposes key queries via
-- emu:getKey / emu:getKeys. One call site uses this, so a minimal shape is enough —
-- but it must return SOMETHING the reader can index rather than nil.
----------------------------------------------------------------------

input = input or {}

input.read = function()
  local ok, mask = pcall(function() return HOST_getKeys(HOST_for_self) end)
  if not ok then return {} end
  return {
    -- mGBA returns a bitmask; expose both the mask and a keys table so a reader can
    -- use whichever it expects.
    mask = mask,
    A = false, B = false, up = false, down = false, left = false, right = false,
    start = false, select = false,
  }
end

----------------------------------------------------------------------
-- speech output
--
-- The readers ultimately produce text. How that reaches a screen reader is a HOST
-- concern: on Windows the shim writes it to the mGBA console, and the Open Game Access
-- layer (Phase C) is what turns it into speech. Keeping it here means the readers emit
-- text without knowing anything about accessibility plumbing.
----------------------------------------------------------------------

local speech_sink = function(text, interrupt)
  log(text)
end

function oga_set_speech_sink(fn)
  speech_sink = fn
end

function oga_say(text, interrupt)
  speech_sink(text, interrupt ~= false)
end

log("shim installed; host platform = " .. tostring(emu.platform()))
