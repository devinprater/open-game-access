local LOG = "C:\\Users\\Devin Prater\\ps1-reader\\bh20.log"
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
local BASE, SIZE, CHUNK = 0x80000000, 0x400000, 0x10000
local function snapshot()
  local t = {}
  for off = 0, SIZE - CHUNK, CHUNK do
    t[#t + 1] = memory.read_bytes_as_string(BASE + off, CHUNK)
  end
  return t
end
pcall(function() savestate.load("C:\\Users\\Devin Prater\\ps1-reader\\battle2.state") end)
frames(60)
local before = snapshot()
log("snapshot taken")
hold("A", 10, 200)
hold("C Up", 10, 250)
hold("A", 10, 300)
frames(1500)
local after = snapshot()
local changes = 0
for c = 1, #before do
  local b, a = before[c], after[c]
  if b ~= a then
    for i = 1, #b - 1, 2 do
      local vb = b:byte(i) * 256 + b:byte(i + 1)
      local va = a:byte(i) * 256 + a:byte(i + 1)
      if vb >= 1 and vb <= 400 and va < vb and va >= 0 then
        changes = changes + 1
        if changes <= 40 then
          log(string.format("%08X %d -> %d", BASE + (c - 1) * CHUNK + i - 1, vb, va))
        end
      end
    end
  end
end
log("total decreases " .. changes)
log("done")
f:close()
