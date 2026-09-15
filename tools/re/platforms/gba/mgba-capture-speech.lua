-- THE DELIVERABLE: capture real speech from the real reader in real mGBA.
local OUT = "C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local f  = io.open(OUT, "w")
local lf = io.open(LOG, "w")
local n = 0
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
-- the sink the bootstrap's tolk stub calls
_G.oga_say = function(t, intr)
  n = n + 1
  if f then f:write(tostring(t).."\n"); f:flush() end
end
console = { log = function(_, m) w(m) end }
w("=== start frame "..tostring(emu:currentFrame()).." ===")
-- warm up so identification sees a real cart state, then hand over to the reader
for i = 1, 240 do pcall(function() emu:runFrame() end) end
w("=== warmed to frame "..tostring(emu:currentFrame()).." ===")
dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
w("=== bootstrap returned (reader loop ended); utterances="..n.." frame="..tostring(emu:currentFrame()))
if f then f:close() end
if lf then lf:close() end
