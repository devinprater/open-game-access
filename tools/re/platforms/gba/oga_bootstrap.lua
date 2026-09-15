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

-- ⛔ ALSO register it as a MODULE, not just a global. The FFI modules do
-- `local ffi = require "ffi"`, and `require` consults package.preload/package.loaded — it
-- does NOT look at globals. Setting only _G.ffi left require failing with a full
-- package.path dump, which reads like a missing library rather than a missing registration.
-- Both are set so either style works.
package.loaded["ffi"] = _G.ffi
package.preload["ffi"] = function() return _G.ffi end

----------------------------------------------------------------------
-- 1b. restore LuaJIT's `module()` (REMOVED in Lua 5.2+)
--
-- ⛔ a-star.lua line 26 is `module("astar", package.seeall)`. That is LuaJIT/Lua 5.1
-- syntax; Lua 5.2 removed `module()`, so on mGBA's 5.4 it fails with
-- `attempt to call a nil value (global 'module')`. It is the FIRST thing that runs during
-- the reader's boot (pokemon.lua does `require "a-star"` on line 1), so without this the
-- reader never starts at all.
--
-- Only a-star.lua uses it, so a small compatible implementation is enough — no need to
-- port the file. Implemented per the Lua 5.1 reference: create the table, set it as a
-- global, capture the calling module's environment, and honour `package.seeall`.
----------------------------------------------------------------------

if _G.module == nil then
  _G.module = function(name, ...)
    local mod = package.loaded[name]
    if mod == nil then mod = {} end
    mod._NAME = name
    mod._M = mod
    mod._PACKAGE = name:match("^(.*%.)") or ""
    package.loaded[name] = mod
    -- The 5.1 global for a module name, e.g. `astar`.
    _G[name] = mod

    -- package.seeall: give the module read access to the global environment via its
    -- metatable __index. This is what a-star.lua relies on to reach globals like
    -- string/table/math without qualifying them.
    for _, opt in ipairs({...}) do
      if opt == package.seeall then
        setmetatable(mod, { __index = _G })
      end
    end

    -- Redirect the caller's environment into the module table, which is what makes
    -- `function foo()` inside the file define mod.foo rather than a global.
    local caller = debug.getinfo(2, "f")
    if caller and caller.func then
      local i = 1
      while true do
        local n = debug.getupvalue(caller.func, i)
        if n == nil then break end
        if n == "_ENV" then
          debug.setupvalue(caller.func, i, mod)
          break
        end
        i = i + 1
      end
    end
    return mod
  end
  -- Sentinel so `module("x", package.seeall)` matches the reference implementation.
  package.seeall = package.seeall or function() end
end

----------------------------------------------------------------------
-- 1c. restore LuaJIT's `bit` library
--
-- ⛔ The readers call `bit.*` 119 times and NEVER `require` it — it is a LuaJIT built-in.
-- Lua 5.4 has no `bit` (5.2's `bit32` was removed in 5.3), so gba.lua fails at line 489
-- with `attempt to index a nil value (global 'bit')`.
--
-- This is not cosmetic: `bit.band`/`bit.lshift` are how the readers test display-register
-- flags, so without it the reader cannot read display state at all. See oga_bit.lua for the
-- implementation and the signed-32-bit notes.
----------------------------------------------------------------------

local bitpath = BOOT_DIR .. "oga_bit.lua"
local bitchunk, biterr = loadfile(bitpath)
if bitchunk then
  local ok, err = pcall(bitchunk)
  if ok then
    log("pure-Lua `bit` library installed")
  else
    log("!! oga_bit.lua failed: " .. tostring(err))
  end
else
  log("!! could not load " .. bitpath .. ": " .. tostring(biterr))
end

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

-- ⛔ REGISTER IT AS A MODULE TOO. pokemon.lua's boot does `tolk = require "tolk"` at the
-- top level (line 1052), and `require` consults package.preload/package.loaded — not
-- globals. Setting only _G.tolk left require searching package.path for tolk.lua, finding
-- the REAL FFI tolk.lua, and failing on ffi.load("tolk") with "The specified module could
-- not be found". Registering here short-circuits that.
package.loaded["tolk"] = tolk
package.preload["tolk"] = function() return tolk end

----------------------------------------------------------------------
-- 2b. neutralise the native audio.dll load
--
-- ⛔ pokemon.lua line 1053, at the TOP LEVEL before the main loop:
--
--     assert(package.loadlib("audio.dll", "luaopen_audio"))()
--
-- audio.dll is a native C module. It is not present in the reader set, and it cannot be
-- loaded from mGBA's Lua anyway. Because the line is unguarded and runs during boot, it
-- ABORTS THE READER before the main loop starts — so nothing speaks at all.
--
-- Two things make stubbing it safe rather than a fudge:
--   1. `audio.` is never referenced anywhere in the reader set — verified by grep. The
--      module is loaded and then never used, so a stub changes no behaviour.
--   2. `assert()` requires loadlib to return a FUNCTION, then calls it. So the stub must
--      return a callable that yields something harmless, or the assert fires.
--
-- ⛔ This does NOT fake audio. If sound cues are ever wanted, they must come from the
-- host (mGBA/OGA), not from a pretend DLL — see the sound callback in the Android bridge,
-- which is the real pattern.
----------------------------------------------------------------------

local real_loadlib = package.loadlib
package.loadlib = function(path, init)
  if path == "audio.dll" then
    log("audio.dll load bypassed (module is never used by the reader; no native audio in mGBA)")
    return function() return {} end
  end
  return real_loadlib(path, init)
end

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
-- 3b. preload pure-Lua replacements for the FFI modules
--
-- ⛔ package.preload, NOT a global. The readers do `require "crc32"` and
-- `require "encoding"`, and mGBA's package.path does not include the reader dir by
-- default, so the real FFI versions would be found first (or not at all). Preloading by
-- module name makes `require` return our implementation and never touch the file on disk
-- — which is the only reliable way to keep the FFI versions from loading.
----------------------------------------------------------------------

local purepath = BOOT_DIR .. "oga_pure.lua"
local purechunk, pureerr = loadfile(purepath)
if purechunk then
  local ok, pure = pcall(purechunk)
  if ok and pure then
    package.preload["crc32"] = function() return { crc32 = pure.crc32 } end
    package.preload["encoding"] = function() return pure.encoding end
    log("pure-Lua crc32 + encoding installed")
  else
    log("!! oga_pure.lua failed: " .. tostring(pure))
  end
else
  log("!! could not load " .. purepath .. ": " .. tostring(pureerr))
end

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

-- ⛔ THE READER OWNS THE MAIN LOOP, AND THAT IS A REAL RISK ON mGBA.
--
-- pokemon.lua ends with:
--
--     while true do
--       emu.frameadvance()
--       main_loop()
--     end
--
-- That is the BizHawk model: the script drives the emulator and BizHawk yields control
-- back. mGBA runs scripts on the main thread and `emu:runFrame()` advances a frame, so this
-- shape MAY work — but it is not how mGBA's own examples are written (they use
-- callbacks:add("frame", ...)), and mGBA's changelog contains "Qt: Disable sync while
-- running scripts from main thread".
--
-- ⛔ WHAT TO WATCH FOR IN THE REAL RUN: if mGBA's window freezes and stops responding
-- while the script is loaded, this loop is why. The fix would be to restructure
-- frameadvance() into a frame callback rather than a blocking loop — a change to the SHIM,
-- not to the reader.
--
-- Do not pre-emptively rewrite it: mGBA may handle it fine, and guessing here would mean
-- restructuring the load path around a problem that might not exist. Establish it first.

local ok, err2 = pcall(chunk)
if not ok then
  log("!! reader raised: " .. tostring(err2))
end

log("bootstrap complete")
