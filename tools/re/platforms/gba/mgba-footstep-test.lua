-- FINAL CHECK with a CLEAN shim: the cue dump lives in the HARNESS, not the shim.
local LOG="C:/Users/Devin Prater/AppData/Local/Temp/step5.txt"
local lf=io.open(LOG,"w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say=function(t)
  local s=tostring(t)
  w("SAY: "..s)
  -- dump cues the first time a position line appears after walking started
  if s:match("^x %d") and not _G._dumped and _G.audio and _G.audio.cues then
    _G._dumped = true
    local cues=_G.audio.cues(); local n=0
    for _ in pairs(cues) do n=n+1 end
    w("[cues] total = "..n)
    for i,c in ipairs(cues) do if i<=3 then w(string.format("[cue %d] %s pan=%s vol=%s", i, tostring(c.path), tostring(c.pan), tostring(c.volume))) end end
  end
end
console={log=function(_,m) w(m) end}
local REAL=emu
local function frame(n) for _=1,n do pcall(function() REAL:runFrame() end) end end
local function tap(k,h) REAL:setKeys(2^k); frame(h or 5); REAL:setKeys(0); frame(5) end
frame(260)
for i=1,150 do tap(0,4) end
for i=1,120 do tap(3,4); tap(0,4) end
for i=1,300 do tap(0,4) end
for i=1,12 do tap(1,6) end          -- B ONLY: dismiss without re-triggering the object
_G.oga_pad_script_pending = {
  { at=200, pad={"DOWN"},  release_at=300 },
  { at=400, pad={"DOWN"},  release_at=500 },
  { at=600, pad={"RIGHT"}, release_at=700 },
}
_G.oga_key_script_pending = { { at=900, keys={"Y"}, release_at=905 } }
dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
