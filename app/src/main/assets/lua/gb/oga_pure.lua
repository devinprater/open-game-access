-- oga_pure.lua — pure-Lua replacements for the FFI-dependent reader modules.
--
-- ⛔ WHY THIS FILE EXISTS. Four of the 166 reader files require LuaJIT's `ffi`, which
-- mGBA's stock Lua 5.4 does not have. Two of them provide real functionality that the
-- readers USE, so stubbing them with no-ops would break game identification rather than
-- host compatibility:
--
--   crc32.lua   — identifies which game/hack is loaded (a reader calls crc32(rom_bytes))
--   encoding.lua — UTF-16 round-trip, needed only because Tolk wants UTF-16
--
-- This file provides working pure-Lua versions of both, so the readers keep their real
-- behaviour instead of degrading to a stub. The other two (tolk, win-controls) are
-- genuinely host-specific and are stubbed in oga_bootstrap.lua instead.
--
-- Interfaces copied from the originals so the readers need no changes:
--   crc32(src, crc)  -> number      src may be a STRING or a TABLE of byte values
--   encoding.to_utf16(s) / to_utf8(s) -> string

----------------------------------------------------------------------
-- crc32 (standard CRC-32, IEEE 802.3 polynomial 0xEDB88320)
--
-- ⛔ THE INPUT MAY BE A TABLE. The readers call this with the result of
-- memory.readbyterange(), which our shim deliberately returns as a 1-based TABLE (to match
-- BizHawk). A pure-Lua crc32 that only accepts strings would fail here in a way that
-- looks like a wrong checksum rather than a type error — so accept both, exactly as the
-- original does.
----------------------------------------------------------------------

local crc32_table = nil

local function build_crc32_table()
  local t = {}
  for i = 0, 255 do
    local c = i
    for _ = 1, 8 do
      if c % 2 == 1 then
        c = 0xEDB88320 ~ (c // 2)
      else
        c = c // 2
      end
    end
    t[i] = c
  end
  return t
end

local function crc32_bytes(getbyte, length, crc)
  if not crc32_table then crc32_table = build_crc32_table() end
  -- Standard CRC-32 is computed on the one's complement of the running value.
  local c = (crc or 0) ~ 0xFFFFFFFF
  for i = 1, length do
    local b = getbyte(i)
    c = crc32_table[(c ~ b) & 0xFF] ~ (c // 256)
  end
  return (c ~ 0xFFFFFFFF) & 0xFFFFFFFF
end

local function crc32(src, crc)
  local t = type(src)
  if t == "string" then
    return crc32_bytes(function(i) return src:byte(i) end, #src, crc)
  elseif t == "table" then
    -- 1-based, matching the shim's readbyterange output.
    return crc32_bytes(function(i) return src[i] or 0 end, #src, crc)
  end
  return nil
end

----------------------------------------------------------------------
-- encoding — identity functions
--
-- The originals convert to/from UTF-16 because Tolk's C API takes wchar_t. With speech
-- routed away from Tolk (see oga_bootstrap.lua), the readers' own text is already in the
-- form we need, so identity is CORRECT here rather than a shortcut: any consumer of these
-- values in our path wants the byte string unchanged.
--
-- ⛔ Note this is a real behavioural difference from the original, and it is safe only
-- because nothing in our path calls the Tolk C API. If Tolk is ever reintroduced, this
-- must become a real UTF-16 conversion.
----------------------------------------------------------------------

local encoding = {
  to_utf16 = function(s) return s end,
  to_utf8 = function(s) return s end,
}

return { crc32 = crc32, encoding = encoding }
