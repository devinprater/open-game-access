-- core/mem.lua — reading and writing the console's main RAM
--
-- SHARED BY EVERY GAME. Nothing in this file knows which Pokémon game is running; if you find
-- yourself wanting a game-specific fact in here, it belongs in the game file instead.
-- Loaded by a game file with:  local mem = core("mem")
--
-- MOVED here from games/bw.lua on 2026-09-04, verbatim — the comments are the expensive part.
-- Every claim they make about BizHawk's internals was verified against BizHawk's own source at
-- the time, and most of them cost a test round to learn.
-- ⚠ DS-SPECIFIC, not game-specific: the base and the 4 MiB domain are the Nintendo DS's, so when
-- the roadmap reaches the 3DS this is one of the two files that grows a platform branch (the other
-- is the loader's rom_code()).
local MM = 0x02000000                 -- RAM base (both games). Reads are RAW; the Black -0x20 lives on the
                                      -- FIXED addresses via DELTA (see below), NOT on the base — because a
                                      -- pointer VALUE read from Black RAM is already Black-native (shifting it
                                      -- again double-subtracts; that was the "only options worked" bug).
local DOMAIN = 0x400000               -- melonDS "Main RAM" size (4 MiB).
-- ⛔ NIL-TOLERANT ON PURPOSE (2026-09-01). A nil address is, self-evidently, not a RAM address, and
-- answering false is the fail-closed answer every caller already expects. Before this, one nil reaching
-- here raised a Lua error, and a Lua error does not silence one reader — it kills the SCRIPT, so a blind
-- player loses every bit of speech mid-session with no way back except reloading. That is the worst
-- failure this tool has, and it is not worth risking to catch a caller's bug half a second earlier.
-- (The real bug is still fixed at its source; this is the net under it.)
local function inram(a) return a ~= nil and a >= 0x02000000 and a < 0x02400000 end
-- inrange guards the domain OFFSET: once Black shifts the base, a stray/garbage pointer can compute an
-- out-of-domain offset; returning 0 instead of letting mainmemory throw keeps the frame loop alive. It
-- never triggers on White (every live read is already in-domain) -> White behaviour is byte-for-byte identical.
-- ⛔ IT TAKES A WIDTH, AND THAT IS THE WHOLE POINT (codex-review 2026-09-04, confirmed). The old
-- version tested only the FIRST byte, so a garbage pointer landing in the last three bytes of Main
-- RAM — say 0x023FFFFA, which inram happily accepts — let u32 call read_u32_le(0x3FFFFE) and read
-- straight off the end of the domain. Whether BizHawk clamps, wraps or THROWS there was never
-- established, and if it throws, the whole script dies and a blind player loses every bit of speech
-- mid-session with no way to tell that from a quiet moment in the game (Rule 10). Checking the full
-- span costs one addition and removes the question. Nothing legitimate lives in the last few bytes
-- of Main RAM, so no real read changes answer.
local function inrange(o, w) return o ~= nil and o >= 0 and o + w <= DOMAIN end
local function u8(a)  local o = a - MM; return inrange(o, 1) and mainmemory.read_u8(o) or 0 end
local function u16(a) local o = a - MM; return inrange(o, 2) and mainmemory.read_u16_le(o) or 0 end
local function u32(a) local o = a - MM; return inrange(o, 4) and mainmemory.read_u32_le(o) or 0 end
local function w8(a, v) local o = a - MM; if inrange(o, 1) then mainmemory.write_u8(o, v) end end
-- SIGNED 32-bit. Added 2026-09-04 for Gen IV, where the engine stores tile coordinates as plain `int`
-- (MapObject.x/.z, read by MapObject_GetX/GetZ) — and a coordinate that goes negative read as unsigned
-- becomes ~4.29 billion, which is not a wrong number so much as a number that makes every bearing,
-- distance and A* cost downstream of it nonsense. Same fail-closed shape as u32: an unreadable address
-- answers 0, because a raised error would kill the whole script (Rule 10).
local function s32(a)
	local v = u32(a)
	return v >= 0x80000000 and v - 0x100000000 or v
end

return {
	MM = MM, DOMAIN = DOMAIN,
	-- inrange is deliberately NOT exported: it is the internal span guard and it now REQUIRES a
	-- width, so an outside caller passing only an offset would raise rather than fail closed.
	inram = inram,
	u8 = u8, u16 = u16, u32 = u32, s32 = s32, w8 = w8,
}
