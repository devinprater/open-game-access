local LOG = "C:\\Users\\Devin Prater\\ps1-reader\\bh17.log"
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
local HP_P, HP_E = 0x800AE6B6, 0x800AEA76
local function hp() return memory.read_u16_be(HP_P), memory.read_u16_be(HP_E) end
pcall(function() savestate.load("C:\\Users\\Devin Prater\\ps1-reader\\battle2.state") end)
frames(60)
log(string.format("start P=%d E=%d", hp()))
hold("A", 10, 200)
log(string.format("after A P=%d E=%d", hp()))
hold("C Up", 10, 250)
log(string.format("after CUp P=%d E=%d", hp()))
hold("A", 10, 300)
for i = 1, 12 do
  frames(150)
  log(string.format("t+%d P=%d E=%d", i * 150, hp()))
end
log("done")
f:close()
