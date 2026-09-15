-- GB/GBС live test: drive a Game Boy game into play, then load the reader and press hotkeys.
-- The GB path differs from GBA: gb.lua auto-reads TEXT when the screen content changes, so it
-- may speak without any hotkey at all.
local OUT="C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOG="C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local f=io.open(OUT,"w"); local lf=io.open(LOG,"w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say=function(t) if f then f:write(tostring(t).."\n"); f:flush() end end
console={log=function(_,m) w(m) end}
local REAL=emu
local function frame(n) for _=1,n do pcall(function() REAL:runFrame() end) end end
local function pad(k,h) REAL:setKeys(1<<k); frame(h or 5); REAL:setKeys(0); frame(4) end
w("=== booting GB game ===")
frame(300)
-- GB games: splash -> title -> (continue/new) -> gameplay. Mash START/A generously.
for i=1,60 do pad(3,5) end
for i=1,120 do pad(0,5) end
w("=== frame "..tostring(REAL:currentFrame()).." — loading reader ===")
_G.oga_key_script_pending = {
  { at=200, keys={"Y"}, release_at=206 },
  { at=500, keys={"M"}, release_at=506 },
  { at=800, keys={"Y"}, release_at=806 },
}
dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
w("=== reader returned ===")
