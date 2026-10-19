local LOG = "C:\\Users\\Devin Prater\\ps1-reader\\bh25.log"
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
local function shot(name) pcall(function() client.screenshot("C:\\Users\\Devin Prater\\ps1-reader\\shots\\" .. name .. ".png") end) end
pcall(function() savestate.load("C:\\Users\\Devin Prater\\ps1-reader\\battle2.state") end)
frames(900)
shot("m1")
hold("A", 10, 200)
shot("m2")
hold("C Up", 10, 200)
shot("m3")
hold("A", 10, 600)
shot("m4")
log("done")
f:close()
