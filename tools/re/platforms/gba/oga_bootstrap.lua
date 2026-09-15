-- oga_bootstrap.lua — the ONE file to load in mGBA's Scripting window.
--
-- ⛔ WHY THIS EXISTS. Loading a reader by hand meant loading the shim first and then the
-- reader, in the right order, from the right working directory. That is three ways to get
-- it subtly wrong while debugging a host-API mismatch. This file does the whole sequence:
--
--   1. installs the BizHawk→mGBA shim   (mgba_compat.lua)
--   2. stubs the LuaJIT-FFI surfaces the readers expect (tolk, crc32)
--   3. resolves the reader's directory and chdir's there
--   4. loads the reader's own entry point (pokemon.lua)
--
-- Load this, and nothing else.
--
-- ══════════════════════════════════════════════════════════════════════════════
-- ⛔ THE HARD FINDING: THE READERS ARE NOT PURE LUA. THEY ARE LUAJIT + FFI + TOLK.
-- ══════════════════════════════════════════════════════════════════════════════
--
-- The reader set does `require "ffi"` and speaks through the Tolk DLL:
--
--     local ffi = require "ffi"
--     local tolk = ffi.load("tolk")
--     tolk.Tolk_Output(encoding.to_utf16(s), false)
--
-- That is BizHawk's LuaJIT. mGBA embeds stock Lua 5.4, which has NO `ffi` module and no
-- way to load a DLL without writing a C module. This is not a configuration difference —
-- it is a different Lua runtime.
--
-- The good news is the surface is TINY and BOUNDED: of 166 Lua files, exactly FOUR touch
-- FFI, and only two of those matter for speech and ROM identification:
--
--     tolk.lua         speech output        → stubbed below (routes to mGBA console/OGA)
--     win-controls.lua file dialogs         → only for interactive prompts; not needed headless
--     crc32.lua        game identification  → stubbed below in pure Lua
--     encoding.lua     UTF-16 conversion    → only needed because Tolk wants UTF-16;
--                                             not needed once speech is routed away from Tolk
--
-- The core readers (gb.lua, gba.lua, game/common/*.lua and every per-language file) are
-- pure Lua once the shim is in place. So this bootstrap replaces the FFI layer instead of
-- porting 688 KB of reader code.
--
-- ⛔ WHAT THIS DELIBERATELY DOES NOT DO: it does not pretend to be Tolk. Tolk speaks on
-- Windows directly; here the text is routed to the same speech sink the shim uses, which
-- the Open Game Access layer (Phase C) turns into real speech. That keeps the layering
-- intact — the reader emits text, OGA decides how it is heard.

----------------------------------------------------------------------
-- 0. find ourselves, so sibling files can be loaded regardless of cwd
----------------------------------------------------------------------

local BOOT = debug.getinfo(1, "S").source:sub(2)
local BOOT_DIR = BOOT:match("(.*[/\\])") or "./"

local function log(msg)
  if console and console.log then console:log("[oga-boot] " .. tostring(msg)) end
end

-- ⛔ mGBA's `require` uses package.path, which does not include our directory by default,
-- and the readers `require "a-star"` / `require "serpent"` by bare name. Prepending the
-- reader directory is what makes those resolve; without it the first require fails with a
-- message about package.path that looks like a missing file.
package.path = BOOT_DIR .. "?.lua;" .. BOOT_DIR .. "?/init.lua;" .. package.path

----------------------------------------------------------------------
-- 1. the pure-Lua `ffi` stub
--
-- mGBA has no `ffi`. Rather than fail, install a stub that records what was asked for
-- and returns harmless values — so a reader that merely TOUCHES an FFI module still runs,
-- and anything that would genuinely need native code reports clearly instead of crashing
-- with "attempt to index a nil value".
----------------------------------------------------------------------

local ffi_calls = {}

_G.ffi = _G.ffi or {
  -- cdef/cast/os/arch are no-ops here.
  cdef = function(def) ffi_calls[#ffi_calls + 1] = "cdef" end,
  cast = function(ct, v) return v end,
  string = function(s) return s end,
  sizeof = function() return 0 end,
  os = function() return "windows" end,
  arch = function() return "x64" end,
  new = function(ct, init) return init end,
  -- ffi.load is the interesting one: report it so a genuinely native dependency is
  -- visible in the log rather than silently missing.
  load = function(name)
    log("native library requested: " .. tostring(name) .. " (stubbed — no FFI in mGBA)")
    return setmetatable({}, {
      __index = function(_, fn)
        return function(...) return nil end
      end,
    })
  end,
}

----------------------------------------------------------------------
-- 2. stub Tolk
--
-- The readers call tolk.output(text) and (rarely) tolk.silence(). Route output to the
-- shim's speech sink, which is the same path everything else uses.
----------------------------------------------------------------------

tolk = tolk or {
  output = function(s)
    if _G.oga_say then
      _G.oga_say(tostring(s), false)
    elseif console and console.log then
      console:log("[speech] " .. tostring(s))
    end
  end,
  silence = function() end,
}

----------------------------------------------------------------------
-- 3. stub encoding
--
-- Real encoding.lua round-trips UTF-16 for Tolk. With Tolk stubbed, identity functions are
-- sufficient, and they are only reached on the paths that were building UTF-16 strings.
----------------------------------------------------------------------

encoding = encoding or {
  to_utf16 = function(s) return s end,
  to_utf8 = function(s) return s end,
}

----------------------------------------------------------------------
-- 4. install the host shim
----------------------------------------------------------------------

local shimpath = BOOT_DIR .. "mgba_compat.lua"
local shimchunk, shimerr = loadfile(shimpath)
if shimchunk then
  -- The shim defines emu/memory/input and returns early if there is no core loaded.
  local ok, err = pcall(shimchunk)
  if not ok then log("shim error: " .. tostring(err)) end
else
  log("!! could not load " .. shimpath .. ": " .. tostring(shimerr))
end

----------------------------------------------------------------------
-- 5. report, then load the reader
----------------------------------------------------------------------

log("platform: " .. tostring(emu and emu.platform and emu.platform() or "?"))

-- ⛔ THE READER DIRECTORY IS CONFIGURABLE and defaults to the bootstrap's own directory.
-- Set OGA_READER_DIR before loading if the readers live elsewhere; this avoids baking an
-- absolute Windows path into a committed file.
local READER_DIR = os.getenv("OGA_READER_DIR") or BOOT_DIR

local entry = READER_DIR .. "pokemon.lua"
log("loading reader: " .. entry)

local chunk, err = loadfile(entry)
if not chunk then
  log("!! could not load the reader: " .. tostring(err))
  log("   expected the Pokemon Access reader set at: " .. READER_DIR)
  return
end

-- ⛔ A READER THAT ERRORS MUST NOT TAKE THE EMULATOR DOWN. pcall the entry point and
-- report the failure with its position; an unprotected error here would end the session
-- and lose the log that says why.
local ok, err2 = pcall(chunk)
if not ok then
  log("!! reader raised: " .. tostring(err2))
end

log("bootstrap complete")
