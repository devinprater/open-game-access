-- oga-live-test.lua — THE LIVE-STATE TEST.
--
-- Drive a new game into the overworld, THEN load the reader in the SAME process, then press
-- real hotkeys. This is the first test where the reader has genuine game state to read:
-- a map, a player position, tiles, and text.
--
-- Why the same process: saving needs the in-game menu, and a fresh save left the cartridge
-- blank (verified: the FireRed .sav is 100% 0xFF). Driving and reading in one run avoids the
-- save-file problem entirely.
--
-- Input sequence learned from screenshots (see mgba-reach-overworld.lua):
--   splash/title -> Oak's speech (A-mash) -> name screens (START opens confirm, A accepts)
--   -> long dialogue -> player's bedroom, which IS overworld gameplay (ow19.png).

local OUT = "C:/Users/Devin Prater/AppData/Local/Temp/oga-speech.txt"
local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local SH  = "C:/Users/Devin Prater/AppData/Local/Temp/live"
local f  = io.open(OUT, "w")
local lf = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end
_G.oga_say = function(t) if f then f:write(tostring(t).."\n"); f:flush() end end

console = { log = function(_, m) w(m) end }

local REAL = emu
local function frame(n) for _ = 1, n do pcall(function() REAL:runFrame() end) end end
local function pad(k, hold) REAL:setKeys(1 << k); frame(hold or 4); REAL:setKeys(0); frame(3) end
local A, B, START, DOWN, UP = 0, 1, 3, 7, 6

local n = 0
local function shot()
  n = n + 1
  pcall(function() REAL:screenshot(string.format("%s%02d.png", SH, n)) end)
end

_G.oga_diag = true
w("=== phase 1: boot + Oak's speech ===")
frame(260)
for i = 1, 150 do pad(A, 4) end

w("=== phase 2: name screens ===")
for i = 1, 120 do
  pad(START, 4); pad(A, 4)
  if i % 3 == 0 then pad(A, 4) end
  if i % 10 == 0 then pad(B, 4) end
end

w("=== phase 3: finish intro, reach the overworld ===")
for i = 1, 300 do pad(A, 4) end
shot()

-- Move around a bit so the reader has a position change to detect (footsteps, tile reads).
w("=== phase 4: walk ===")
for i = 1, 12 do pad(DOWN, 6); pad(A, 4) end
for i = 1, 6 do pad(UP, 6) end
shot()

w("=== frame " .. tostring(REAL:currentFrame()) .. " — loading reader on LIVE state ===")

-- Hotkeys scheduled INSIDE the reader's own loop. The reader owns `while true`, so a harness
-- cannot set a key and wait; poll_hooks runs on every frame it requests.
_G.oga_key_script_pending = {
  { at = 200,  keys = {"Y"}, release_at = 205 },   -- current position
  { at = 500,  keys = {"M"}, release_at = 505 },   -- current map name
  { at = 800,  keys = {"Y"}, release_at = 805 },
  { at = 1100, keys = {"T"}, release_at = 1105 },  -- text on screen
  { at = 1500, keys = {"E"}, release_at = 1505 },  -- surrounding tiles
  { at = 1900, keys = {"M"}, release_at = 1905 },
  { at = 2300, keys = {"Y"}, release_at = 2305 },
  { at = 2800, keys = {"T"}, release_at = 2805 },
}

dofile("C:/Users/Devin Prater/Dropbox/programs/pokemon-access/lua/oga_bootstrap.lua")
w("=== reader returned ===")
shot()
