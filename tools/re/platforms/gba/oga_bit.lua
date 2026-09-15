-- oga_bit.lua — pure-Lua implementation of LuaJIT's `bit` library.
--
-- ⛔ WHY THIS IS NEEDED. `bit` is a LuaJIT BUILT-IN — it is not `require`d anywhere in the
-- 166 reader files, it is just expected to exist. mGBA's stock Lua 5.4 has no `bit`
-- (5.3+ replaced it with native operators, and 5.2 had `bit32` which was removed in 5.3).
-- So on mGBA the readers fail with:
--
--     gba.lua:489: attempt to index a nil value (global 'bit')
--
-- ⛔ AND IT IS NOT A MINOR USAGE. The readers call it 119 times:
--
--     bit.band    65     bit.rshift  36     bit.lshift  9
--     bit.bor      7     bit.bnot     2
--
-- These are real game-state reads — e.g. `bit.band(memory.readbyte(RAM_BGS + 17),
-- bit.lshift(1, id)) ~= 0` decides whether a background layer is active. Without `bit`,
-- the reader cannot read display state at all.
--
-- ⛔ SIGNEDNESS MATTERS. LuaJIT's bit library operates on 32-bit SIGNED integers, and the
-- readers rely on that: `bit.band` with a negative operand, or `bit.rshift` producing a
-- value that is then compared, behaves per the signed 32-bit contract. Returning unsigned
-- values would produce numbers that look right for small cases and wrong at the boundaries,
-- so every function here normalises to signed 32-bit, matching LuaJIT.

local function to_int32(x)
  x = x % 0x100000000
  if x >= 0x80000000 then x = x - 0x100000000 end
  return x
end

local function to_uint32(x)
  return x % 0x100000000
end

local bitlib = {}

-- band / bor are simplest computed over the unsigned representation, then normalised.
function bitlib.band(...)
  local n = select("#", ...)
  if n == 0 then return 0 end
  local r = to_uint32(select(1, ...))
  for i = 2, n do r = r & to_uint32((select(i, ...))) end
  return to_int32(r)
end

function bitlib.bor(...)
  local n = select("#", ...)
  if n == 0 then return 0 end
  local r = to_uint32(select(1, ...))
  for i = 2, n do r = r | to_uint32((select(i, ...))) end
  return to_int32(r)
end

function bitlib.bxor(...)
  local n = select("#", ...)
  if n == 0 then return 0 end
  local r = to_uint32(select(1, ...))
  for i = 2, n do r = r ~ to_uint32((select(i, ...))) end
  return to_int32(r)
end

function bitlib.bnot(x)
  return to_int32(~to_uint32(x))
end

-- ⛔ SHIFTS: LuaJIT masks the shift count to 5 bits (0-31), so `lshift(x, 32)` is
-- `lshift(x, 0)`, NOT zero. A naive implementation that shifts by the full count returns 0
-- and would break any code that shifts by a computed amount.
function bitlib.lshift(x, n)
  n = to_uint32(n) % 32
  return to_int32(to_uint32(x) << n)
end

function bitlib.rshift(x, n)
  n = to_uint32(n) % 32
  -- LOGICAL shift: fill with zeros regardless of sign, per LuaJIT.
  return to_int32(to_uint32(x) >> n)
end

function bitlib.arshift(x, n)
  n = to_uint32(n) % 32
  return to_int32(to_int32(x) >> n)
end

-- rotate left/right, masking the count like the shifts do
function bitlib.rol(x, n)
  n = to_uint32(n) % 32
  local u = to_uint32(x)
  return to_int32(((u << n) | (u >> (32 - n))) )
end

function bitlib.ror(x, n)
  n = to_uint32(n) % 32
  local u = to_uint32(x)
  return to_int32(((u >> n) | (u << (32 - n))))
end

-- bswap / tobit / tohex round out the library.
function bitlib.bswap(x)
  local u = to_uint32(x)
  local r = ((u & 0xFF) << 24) | (((u >> 8) & 0xFF) << 16)
          | (((u >> 16) & 0xFF) << 8) | ((u >> 24) & 0xFF)
  return to_int32(r)
end

-- tobit normalises like the rest of the library: numbers wrap, non-numbers go through
-- Lua's own conversion rules then wrap.
function bitlib.tobit(x)
  if type(x) ~= "number" then x = tonumber(x) or 0 end
  return to_int32(x)
end

-- tohex produces an uppercase hex string, 8 digits unless the value is negative, where
-- LuaJIT emits the full two's-complement form. Implemented to match closely enough for the
-- readers, which use it only for diagnostics.
function bitlib.tohex(x, n)
  local u = to_uint32(x)
  local s = string.format("%08X", u)
  if n and n > 8 then return string.rep("0", n - 8) .. s end
  return s
end

-- Expose as a global, which is how LuaJIT presents it.
_G.bit = bitlib

-- ⛔ ALSO register as modules, because some Lua code writes `require "bit"` and both spellings
-- appear in the wild across emulator Lua.
package.preload["bit"]  = function() return bitlib end
package.loaded["bit"]   = bitlib

-- bit32 compatibility: Lua 5.2's name for the same thing. Cheap to alias, and it costs
-- nothing to support both spellings.
package.preload["bit32"] = function() return bitlib end
package.loaded["bit32"]  = bitlib

return bitlib
