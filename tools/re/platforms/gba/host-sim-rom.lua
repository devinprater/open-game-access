-- host-sim-rom.lua — run the reader against a REAL Pokémon ROM in a stubbed host.
--
-- ⛔ WHY THIS IS THE MOST VALUABLE TEST AVAILABLE WITHOUT AN EMULATOR.
--
-- The stubbed host so far returned ZERO for every read, so the reader correctly said
-- "game_not_supported". That proved the boot path but nothing about the reader's ROM logic.
--
-- A GBA/GBC ROM is a FILE, and the cartridge header (title, game code, checksum) lives at a
-- fixed offset in it. So the host can serve REAL ROM BYTES for the header reads. The reader
-- then has to actually identify the game — exercising get_game(), the title parse, the
-- checksum path, and the per-game script loading — all without an emulator.
--
-- What this CANNOT do: the reader also reads live RAM (party, position, map). Those reads
-- still return zero, so anything past game identification is meaningless here. But game
-- identification is the gateway: if the reader cannot identify the ROM, nothing else runs.
--
-- Run:  lua host-sim-rom.lua <rom-file> [reader-dir]

local rom_path = arg[1]
local reader_dir = arg[2] or "."
if not rom_path then
  print("usage: lua host-sim-rom.lua <rom-file> [reader-dir]")
  os.exit(2)
end
if reader_dir:sub(-1) ~= "/" and reader_dir:sub(-1) ~= "\\" then
  reader_dir = reader_dir .. "/"
end

-- ---- load the ROM ----------------------------------------------------------
local fh = io.open(rom_path, "rb")
if not fh then print("!! cannot open " .. rom_path) os.exit(1) end
local rom = fh:read("*a")
fh:close()

print("=== host-sim-rom ===")
print("rom:   " .. rom_path)
print("bytes: " .. #rom)

local ext = rom_path:lower():match("%.(%w+)$") or "?"
local is_gba = (ext == "gba")

-- ── the cartridge header ──────────────────────────────────────────────────────
-- GBA: title at 0xA0 (12 bytes), game code at 0xAC, header checksum at 0xBD,
--      computed over 0xA0..0xBC.
-- GB/GBC: title at 0x134 (15 or 11 bytes), manufacturer code at 0x13F,
--      header checksum at 0x14D over 0x134..0x14C.
local title, code
if is_gba then
  title = rom:sub(0xA1, 0xAC)
  code  = rom:sub(0xAD, 0xB0)
else
  -- ⛔ GB TITLE LENGTH IS 27 BYTES, NOT 15. The reader's parse_old_title walks
  -- `title[i]` from i=9 until it hits 0 (gb.lua:659), and the checksum at 0x14E means it
  -- expects indices up to 27 to be addressable. Handing it a 15-byte string leaves
  -- title[i] == nil past index 15 — and in Lua `nil ~= 0` is TRUE, so the walk never
  -- terminates and the reader hangs forever with no error. That is exactly what happened:
  -- all three GB games timed out at 120s with no output.
  --
  -- 0x134 + 26 = 0x14E, i.e. a 27-byte slice reaches the checksum byte, which is where the
  -- reader's own index arithmetic expects the title to end.
  title = rom:sub(0x135, 0x134 + 27)
  code  = rom:sub(0x140, 0x143)
end
print(string.format("platform: %s", is_gba and "GBA (0)" or "GB/GBC (1)"))
print(string.format("title:  %q", title:gsub("%z", "")))
print(string.format("code:   %q", code:gsub("%z", "")))

-- ── the stubbed host, serving ROM bytes where the reader asks for them ────────
local FIX = { advanced = 0 }

local host = {}
-- ⛔ Map a requested emulator address onto a ROM file offset. For GBA the cartridge is
-- mapped at 0x08000000, so offset = addr - 0x08000000. For GB the header sits in the first
-- bank, so a 0x0000-0x7FFF request maps straight through.
local function rom_byte(addr)
  local off
  if is_gba then
    if addr >= 0x08000000 then off = addr - 0x08000000 + 1
    else off = nil end
  else
    if addr < 0x8000 then off = addr + 1 else off = nil end
  end
  if not off or off < 1 or off > #rom then return 0 end
  return rom:byte(off)
end

function host:read8(a) return rom_byte(a) end
function host:read16(a) return rom_byte(a) + rom_byte(a + 1) * 0x100 end
function host:read32(a)
  return rom_byte(a) + rom_byte(a+1) * 0x100 + rom_byte(a+2) * 0x10000 + rom_byte(a+3) * 0x1000000
end
function host:readRange(a, len)
  -- Return a STRING (as mGBA does) so the shim's table conversion is exercised for real.
  local out = {}
  for i = 0, len - 1 do out[i + 1] = string.char(rom_byte(a + i)) end
  return table.concat(out)
end
function host:readRegister() return 0 end
function host:getKeys() return 0 end
function host:platform() return is_gba and 0 or 1 end
function host:runFrame()
  FIX.advanced = FIX.advanced + 1
  -- ⛔ THE READER READS THE WHOLE SCREEN EVERY FRAME, so the frame budget must be generous.
  -- gb.lua's main_loop calls get_screen() unconditionally, which does
  -- `readbyterange(RAM_TEXT, 360)` — 360 bytes per frame, plus ~16 read8 calls. A budget of
  -- 200 frames therefore allows ~72,000 reads, and the reader legitimately needs more than
  -- that to finish its boot before the first frame.
  --
  -- An earlier budget of 200 made the GB games look like they HUNG: they simply ran out of
  -- frames before reaching "Ready", with no output at all. That misdiagnosis cost real
  -- time. The budget below is sized for a screen-reading loop, not for a trivial test.
  if FIX.advanced > tonumber(os.getenv("OGA_ROM_FRAMES") or "65000") then
    error("frame budget reached — game identification has run", 0)
  end
end
function host:currentFrame() return FIX.advanced end

_G.emu = host
local speech = {}
_G.console = {
  log = function(_, m)
    local s = tostring(m)
    speech[#speech + 1] = s
    io.stdout:write("  " .. s .. "\n"); io.stdout:flush()
  end,
  error = function(_, m)
    local s = "ERROR " .. tostring(m)
    speech[#speech + 1] = s
    io.stdout:write("  " .. s .. "\n"); io.stdout:flush()
  end,
  warn = function(_, m) _G.console.log(nil, m) end,
}
_G.callbacks = { add = function() return 1 end, remove = function() end }

-- ---- run --------------------------------------------------------------------
print()
print("=== reader output ===")
assert(loadfile(reader_dir .. "oga_bootstrap.lua"))()

-- ---- verdict ----------------------------------------------------------------
print()
local joined = table.concat(speech, "\n")
local identified = not joined:find("game_not_supported")
local budget = joined:find("frame budget") ~= nil

print("=== verdict ===")
print(string.format("  reader reached its loop        %s", budget and "yes" or "NO"))
print(string.format("  game identified                %s", identified and "YES" or "no (game_not_supported)"))
if identified then
  for _, l in ipairs(speech) do
    -- The reader's identification lines are the interesting ones.
    if l:find("oga%-shim") and not l:find("installed") then print("    " .. l) end
  end
end
print()
if budget and identified then print("ROM-ID PASS") else print("ROM-ID INCOMPLETE") end
