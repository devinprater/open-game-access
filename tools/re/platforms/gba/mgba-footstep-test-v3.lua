-- FOOTSTEP VERIFICATION v3. Hypothesis: the A in my "dismiss" loop RE-TRIGGERED the NES that
-- the player is standing next to, so the dialogue never closed. In Pokemon a box is dismissed
-- by A/B ONCE — but pressing A while still facing an interactive object opens it again.
-- Fix: dismiss with B ONLY (B never triggers objects), then walk.
local LOG="C:/Users/Devin Prater/AppData/Local/Temp/step3.txt"
local lf=io.open(LOG,"w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say=function(t) w("SAY: "..tostring(t)) end
console={log=function(_,m) w(m) end}
local REAL=emu
local function frame(n) for _=1,n do pcall(function() REAL:runFrame() end) end end
local function tap(k,h) REAL:setKeys(2^k); frame(h or 5); REAL:setKeys(0); frame(5) end

w("=== boot ===")
frame(260)
for i=1,150 do tap(0,4) end
for i=1,120 do tap(3,4); tap(0,4) end
for i=1,300 do tap(0,4) end

w("=== dismiss with B ONLY (B cannot re-trigger an object) ===")
for i=1,12 do tap(1,6) end

w("=== walk ===")
for i, dir in ipairs({7,7,7,4,4,7,7,5,5,6,6,7}) do
  REAL:setKeys(2^dir); frame(50); REAL:setKeys(0); frame(8)
end
w("frame after walk: "..tostring(REAL:currentFrame()))
pcall(function() REAL:screenshot("C:/Users/Devin Prater/AppData/Local/Temp/step3.png") end)

_G.oga_pad_script_pending = {
  { at=200, pad={"DOWN"},  release_at=300 },
  { at=400, pad={"RIGHT"}, release_at=500 },
  { at=600, pad={"DOWN"},  release_at=700 },
}
_G.oga_key_script_pending = {
  { at=900,  keys={"Y"}, release_at=905 },
  { at=1400, keys={"Y"}, release_at=1405 },
}
dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
