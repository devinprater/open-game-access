local LOG = "C:\\Users\\Devin Prater\\ps1-reader\\bh15.log"
local f = io.open(LOG, "w")
local function log(s) f:write(s .. "\n"); f:flush() end
pcall(function() memory.usememorydomain("System Bus") end)
pcall(function() client.unpause() end)
pcall(function() client.speedmode(300) end)
local function frames(n) for i = 1, n do emu.frameadvance() end end
local function hold(btn, n, settle)
  for i = 1, (n or 10) do joypad.set({ [btn] = true }, 1); emu.frameadvance() end
  for i = 1, (settle or 120) do joypad.set({}, 1); emu.frameadvance() end
end
local function frags()
  local out = {}
  for i = 0, 239 do
    local b = memory.read_u32_be(0x800A58F0 + i * 8)
    if b >= 0x80000000 and b < 0x80800000 then out[#out + 1] = i end
  end
  return table.concat(out, ",")
end
local function scan(pat)
  local hits = {}
  local base, size, chunk = 0x80000000, 0x400000, 0x4000
  for off = 0, size - chunk, chunk do
    local b = memory.read_bytes_as_array(base + off, chunk)
    for i = 1, #b - 4 do
      if b[i] == pat[1] and b[i + 1] == pat[2] and b[i + 2] == pat[3] and b[i + 3] == pat[4] then
        hits[#hits + 1] = base + off + i - 1
      end
    end
  end
  return hits
end

pcall(function() savestate.load("C:\\Users\\Devin Prater\\ps1-reader\\battle.state") end)
frames(30)
hold("A", 10, 120)
hold("B", 10, 120)
hold("C Left", 10, 200)
hold("A", 10, 300)
frames(300)
log("battle frags=" .. frags())
pcall(function() savestate.save("C:\\Users\\Devin Prater\\ps1-reader\\battle2.state") end)

local hits = scan({ 53, 163, 91, 83 })
log("charmander records " .. #hits)
for i = 1, math.min(#hits, 6) do
  local a = hits[i]
  local hp = memory.read_u16_be(a + 0x1D)
  log(string.format("  %08X hp-field=%d", a, hp))
end
local first = hits[1]
if first then
  hold("A", 10, 200)
  log(string.format("after A hp=%d", memory.read_u16_be(first + 0x1D)))
  hold("C Up", 10, 200)
  log(string.format("after CUp hp=%d", memory.read_u16_be(first + 0x1D)))
  hold("A", 10, 300)
  frames(600)
  log(string.format("after turn hp=%d frags=%s", memory.read_u16_be(first + 0x1D), frags()))
end
log("done")
f:close()
