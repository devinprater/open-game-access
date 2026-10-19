local LOG = "C:\\Users\\Devin Prater\\ps1-reader\\bh23.log"
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
log("api string=" .. type(memory.read_bytes_as_string) .. " array=" .. type(memory.read_bytes_as_array))
local BASE, SIZE, CHUNK = 0x80000000, 0x400000, 0x10000
local use_string = type(memory.read_bytes_as_string) == "function"
local function snap()
  local t = {}
  for off = 0, SIZE - CHUNK, CHUNK do
    if use_string then
      t[#t + 1] = memory.read_bytes_as_string(BASE + off, CHUNK)
    else
      t[#t + 1] = memory.read_bytes_as_array(BASE + off, CHUNK)
    end
  end
  return t
end
pcall(function() savestate.load("C:\\Users\\Devin Prater\\ps1-reader\\battle2.state") end)
frames(60)
log("snapshot...")
local before = snap()
log("snapshot done")
hold("A", 10, 200)
hold("C Up", 10, 250)
hold("A", 10, 300)
frames(1500)
local after = snap()
log("second snapshot done")
local n = 0
for c = 1, #before do
  local b, a = before[c], after[c]
  local len = use_string and #b or #b
  for i = 1, len - 1, 2 do
    local vb, va
    if use_string then
      vb = b:byte(i) * 256 + b:byte(i + 1)
      va = a:byte(i) * 256 + a:byte(i + 1)
    else
      vb = b[i] * 256 + b[i + 1]
      va = a[i] * 256 + a[i + 1]
    end
    if vb >= 1 and vb <= 400 and va < vb then
      n = n + 1
      if n <= 40 then
        log(string.format("%08X %d -> %d", BASE + (c - 1) * CHUNK + i - 1, vb, va))
      end
    end
  end
end
log("decreases " .. n)
log("done")
f:close()
