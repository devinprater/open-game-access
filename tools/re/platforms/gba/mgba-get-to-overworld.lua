-- oga-get-to-overworld.lua — drive a NEW GAME from the title screen into the overworld,
-- so the reader can be tested against genuinely live game state.
--
-- ⛔ WHY THIS EXISTS. Every save available in this install is unusable for a live test:
--
--   Pokemon - Fire Red Version (U) (V1.1).sav   131072 bytes, ALL 0xFF  -> blank, never saved
--   1986 - Pokemon Emerald (U)(TrashMan).sav    real data, but for a HACK build; boots to a
--                                               gradient with the retail ROM
--   Pokemon - Crystal Version (UE) (V1.1).sav   4.4% non-FF -> sparse/partial
--
-- With a blank save every run was a NEW GAME, which is why the reader said `Ready` and then
-- had no map to narrate: it was sitting in Oak's intro cutscene the whole time.
--
-- So drive the new game through to the overworld ourselves. Then the reader has real state.
--
-- Screenshots at each stage, because memory alone cannot tell "reader broken" from
-- "the game never got there".

local OUT = "C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local SH  = "C:/Users/Devin Prater/AppData/Local/Temp/ow"
local f  = io.open(OUT, "w")
local lf = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say = function(t) if f then f:write(tostring(t).."\n"); f:flush() end end

console = { log = function(_, m) w(m) end }

local REAL = emu
local function frame(n) for _ = 1, n do pcall(function() REAL:runFrame() end) end end
-- mGBA key bits: A=0 B=1 SELECT=2 START=3 RIGHT=4 LEFT=5 UP=6 DOWN=7 R=8 L=9
local function pad(k, hold) REAL:setKeys(1 << k); frame(hold or 4); REAL:setKeys(0); frame(3) end
local A, B, START = 0, 1, 3

local function shot(name)
  pcall(function() REAL:screenshot(SH .. name .. ".png") end)
end

w("=== boot: splash + title ===")
frame(260)
shot("1-title")

-- Title screen -> press START to reach the menu, then A for NEW GAME (CONTINUE is greyed
-- out with a blank save, so NEW GAME is what we want here).
for i = 1, 25 do pad(START, 4) end
for i = 1, 25 do pad(A, 4) end
shot("2-after-start")

-- Oak's intro ("Hello there! Welcome to the world of POKEMON!") is a long A-mash.
w("=== Oak intro: mashing A ===")
for i = 1, 120 do pad(A, 4) end
shot("3-intro")

-- The NAME screen. FireRed shows a text box + on-screen keyboard; START opens the
-- OK/confirm menu. Mash A, then START+A to accept whatever is highlighted.
w("=== name screen ===")
for i = 1, 20 do pad(A, 4) end
for i = 1, 10 do pad(START, 4); pad(A, 4) end
shot("4-name")

-- Rival name, then more intro.
w("=== rival + remaining intro ===")
for i = 1, 60 do pad(A, 4) end
for i = 1, 10 do pad(START, 4); pad(A, 4) end
for i = 1, 80 do pad(A, 4) end
shot("5-after-intro")

w("=== frame " .. tostring(REAL:currentFrame()) .. " — loading reader ===")

-- Now the reader, with hotkeys scheduled INSIDE its own loop (it owns `while true`, so a
-- harness cannot set a key and wait — see poll_hooks in mgba_compat.lua).
_G.oga_key_script_pending = {
  { at = 240,  keys = {"y"}, release_at = 245 },   -- read current position
  { at = 540,  keys = {"m"}, release_at = 545 },   -- read current map name
  { at = 840,  keys = {"y"}, release_at = 845 },
  { at = 1140, keys = {"t"}, release_at = 1145 },  -- read text on screen
  { at = 1500, keys = {"e"}, release_at = 1505 },  -- read surrounding tiles
  { at = 1900, keys = {"y"}, release_at = 1905 },
  { at = 2400, keys = {"m"}, release_at = 2405 },
}

dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
w("=== reader returned ===")
shot("6-final")
