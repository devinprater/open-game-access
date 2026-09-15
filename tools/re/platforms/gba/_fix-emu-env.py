#!/usr/bin/env python3
"""Rewrite the shim's emu/input layers for the PROVEN mGBA design.

Measured on real mGBA 0.11 (dev):

    type(emu)            --> "userdata"
    emu.platform = fn    --> error: Invalid key
    emu = {} ; then REAL.runFrame(REAL)
                         --> error: Function called from invalid context

So `emu` can be neither monkey-patched NOR shadowed. The design that works (100/100 frames
advanced) is: leave the global emu untouched, build a separate BizHawk-shaped table, and give
that table to the READER through the reader chunk's own _ENV.
"""
import pathlib

p = pathlib.Path(r"C:\Users\Devin Prater\Dropbox\programs\pokemon-access\lua\mgba_compat.lua")
text = p.read_text(encoding="utf-8")

# ---------------------------------------------------------------- emu layer
old_emu_start = "local REAL = emu   -- mGBA's CoreAdapter (userdata). Never written to."
old_emu_end = 'emu.framecount = function()\n  return callReal("currentFrame")\nend'
i = text.index(old_emu_start)
j = text.index(old_emu_end) + len(old_emu_end)

new_emu = '''local REAL = emu   -- mGBA's CoreAdapter (userdata). NEVER reassigned, NEVER written to.

-- Fresh colon-bound call on REAL. `fn(REAL, ...)` is the same as `REAL:fn(...)` and does not
-- depend on the global `emu` name, which is what makes it safe here.
local function callReal(name, ...)
  local fn = REAL[name]
  if fn == nil then return nil end
  return fn(REAL, ...)
end

-- The BizHawk surface, as a SEPARATE table. The readers use `emu.frameadvance()` and will
-- resolve `emu` to THIS table once the reader chunk is loaded with the env the bootstrap
-- builds. mGBA's object is never touched, so the core binding never goes invalid.
local BIZ_EMU = {}

BIZ_EMU.platform    = function()      return callReal("platform")     end
BIZ_EMU.frameadvance = function()     return callReal("runFrame")     end
BIZ_EMU.framecount  = function()      return callReal("currentFrame") end
BIZ_EMU.read8       = function(a)     return callReal("read8", a)     end
BIZ_EMU.read16      = function(a)     return callReal("read16", a)    end
BIZ_EMU.read32      = function(a)     return callReal("read32", a)    end
BIZ_EMU.readRange   = function(a, n)  return callReal("readRange", a, n) end
BIZ_EMU.readRegister = function(r)    return callReal("readRegister", r) end
BIZ_EMU.getKeys     = function()      return callReal("getKeys")      end

-- Handed to the reader's environment by oga_bootstrap.lua. Named with the `oga_` prefix so
-- it is obvious this is ours, not mGBA's.
_G.oga_biz_emu = BIZ_EMU'''

text = text[:i] + new_emu + text[j:]

# ---------------------------------------------------------------- input layer
old_input_start = "local REAL_INPUT = input\n\ninput = {}\n\ninput.read = function()"
new_input_start = "local REAL_INPUT = input\n\n-- ⛔ DO NOT DO `input = {}`. `input` is mGBA-OWNED USERDATA in 0.11, so reassigning the global\n-- is the same class of mistake as reassigning `emu`. Build a separate table instead; the\n-- bootstrap gives it to the reader through the same chunk environment.\nlocal BIZ_INPUT = {}\n\nBIZ_INPUT.read = function()"
if old_input_start not in text:
    raise SystemExit("!! input anchor not found")
text = text.replace(old_input_start, new_input_start, 1)

# close: append the export right after the input.read function ends
old_input_tail = """    start = false, select = false,
  }
end"""
new_input_tail = """    start = false, select = false,
  }
end

_G.oga_biz_input = BIZ_INPUT"""
if old_input_tail not in text:
    raise SystemExit("!! input tail anchor not found")
text = text.replace(old_input_tail, new_input_tail, 1)

# ------------------------------------------------- fix the stale comment about "shadowing"
text = text.replace(
    "-- `memory` is genuinely free, which is why only these two need shadowing. Any global the\n"
    "-- shim writes MUST be checked against this list first — assigning to a userdata raises\n"
    "-- \"Invalid key\" and the traceback points at the assignment, not at the real problem.",
    "-- `memory` is genuinely free, so the shim may create it. `emu` and `input` are NOT, and\n"
    "-- neither may be reassigned — see the emu section above for what that costs."
)

p.write_text(text, encoding="utf-8")
print("emu + input layers rewritten")
