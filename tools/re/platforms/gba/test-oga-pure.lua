-- test-oga-pure.lua — verify the pure-Lua crc32 against known-good values.
--
-- ⛔ WHY TEST THIS: crc32 identifies which Pokémon ROM or hack is loaded. A wrong
-- implementation does not error — it returns a plausible number, the reader fails its game
-- lookup, and the symptom is "this game is not supported" for a game that is. That is a
-- silent wrong answer, so the implementation is checked against published vectors.
--
-- Run:  lua test-oga-pure.lua      (from this directory)

local pure = dofile("oga_pure.lua")
local crc32 = pure.crc32

local failures = 0
local function check(label, got, want)
  local ok = got == want
  if not ok then failures = failures + 1 end
  print(string.format("  %-42s %s  got=%-12s want=%s",
        label, ok and "ok  " or "FAIL", tostring(got), tostring(want)))
end

print("crc32 — standard test vectors:")
-- The canonical CRC-32 check value: crc32("123456789") == 0xCBF43926
check('crc32("123456789")', crc32("123456789"), 0xCBF43926)
check('crc32("")', crc32(""), 0x00000000)
check('crc32("a")', crc32("a"), 0xE8B7BE43)
check('crc32("abc")', crc32("abc"), 0x352441C2)
check('crc32("The quick brown fox jumps over the lazy dog")',
      crc32("The quick brown fox jumps over the lazy dog"), 0x414FA339)

print("crc32 — table input (what memory.readbyterange returns):")
-- The readers pass a TABLE of byte values from readbyterange. Same data as a string must
-- give the same checksum, or game identification breaks only in the reader's path.
local function to_table(s)
  local t = {}
  for i = 1, #s do t[i] = s:byte(i) end
  return t
end
check('crc32(table "123456789")', crc32(to_table("123456789")), 0xCBF43926)
check('crc32(table "abc")', crc32(to_table("abc")), 0x352441C2)
check('crc32(table "The quick brown fox...")',
      crc32(to_table("The quick brown fox jumps over the lazy dog")), 0x414FA339)

print("crc32 — incremental (the (src, crc) second argument):")
-- The original signature is crc32(src, crc), so a reader may chain calls.
check('crc32("456789", crc32("123"))', crc32("456789", crc32("123")), 0xCBF43926)

print("crc32 — edge cases:")
check('crc32(nil)', crc32(nil), nil)
check('crc32(42)', crc32(42), nil)
-- Binary data, including high bytes: a signed/unsigned slip would show up here.
local bin = string.char(0x00, 0xFF, 0x80, 0x01, 0x7F)
check('crc32(binary with high bytes)',
      crc32(bin), crc32(to_table(bin)))

print("encoding — identity:")
check('to_utf8(s)', pure.encoding.to_utf8("hi"), "hi")
check('to_utf16(s)', pure.encoding.to_utf16("hi"), "hi")

print()
if failures == 0 then
  print("ALL PASS")
else
  print(failures .. " FAILURE(S)")
  os.exit(1)
end
