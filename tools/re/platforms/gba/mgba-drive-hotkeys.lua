-- CORRECTED: in FireRed's menu CONTINUE is the DEFAULT item. Pressing DOWN selects NEW GAME
-- (which is what the naming screen in the last screenshot proved). Press A alone to continue.
local OUT="C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOG="C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local S="C:/Users/Devin Prater/AppData/Local/Temp/hk"
local f=io.open(OUT,"w"); local lf=io.open(LOG,"w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say=function(t) if f then f:write(tostring(t).."\n"); f:flush() end end
console={log=function(_,m) w(m) end}
local REAL=emu
local function frame(n) for _=1,n do pcall(function() REAL:runFrame() end) end end
local function pad(k,hold) REAL:setKeys(1<<k); frame(hold or 4); REAL:setKeys(0); frame(3) end
frame(200)
for i=1,20 do pad(0,3) end          -- splash -> title
w("--- at title, pressing A (CONTINUE is default) ---")
for i=1,45 do pad(0,3) end          -- CONTINUE + any resume dialogue
w("frame "..tostring(REAL:currentFrame()))
pcall(function() REAL:screenshot(S.."B.png") end)
_G.oga_key_script_pending = {
  { at=300, keys={"y"}, release_at=305 },   -- position
  { at=700, keys={"m"}, release_at=705 },   -- map name
  { at=1100,keys={"y"}, release_at=1105 },
  { at=1600,keys={"t"}, release_at=1605 },  -- text on screen
  { at=2100,keys={"y"}, release_at=2105 },
}
dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
w("reader returned")
