local LOG = "C:\\Users\\Devin Prater\\ps1-reader\\bh24.log"
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
local A = 0x80104B8A
local function dump(addr)
  local t = {}
  for i = -16, 32 do t[#t + 1] = string.format("%02X", memory.read_u8(addr + i)) end
  return table.concat(t, " ")
end
pcall(function() savestate.load("C:\\Users\\Devin Prater\\ps1-reader\\battle2.state") end)
frames(60)
log(string.format("start A=%d", memory.read_u16_be(A)))
log("ctx " .. dump(A))
hold("A", 10, 200)
log(string.format("after A A=%d", memory.read_u16_be(A)))
hold("C Up", 10, 250)
log(string.format("after CUp A=%d", memory.read_u16_be(A)))
hold("A", 10, 300)
for i = 1, 8 do
  frames(150)
  log(string.format("t+%d A=%d", i * 150, memory.read_u16_be(A)))
end
log("ctx2 " .. dump(A))
log("done")
f:close()
