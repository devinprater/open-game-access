-- oga-reach-overworld.lua — grind a new game all the way into the overworld, with periodic
-- screenshots so the exact stopping point is VISIBLE rather than guessed.
--
-- ⛔ WHY THE PREVIOUS ATTEMPT FAILED. Mashing A does not clear FireRed's intro:
--   - the player-name and rival-name screens need START to open the confirm menu, and
--   - there are long dialogue stretches either side of them.
-- Screenshots proved the game was still on Oak's speech and the rival-name prompt after
-- 120 A presses, so this version mixes A / START / B and shoots every 150 frames.
--
-- The naming screens default to a name already entered, so START then A accepts it.

local LOG = "C:/Users/Devin Prater/AppData/Local/Temp/oga-log.txt"
local SH  = "C:/Users/Devin Prater/AppData/Local/Temp/ow"
local lf  = io.open(LOG, "w")
local function w(s) if lf then lf:write(tostring(s).."\n"); lf:flush() end end

console = { log = function(_, m) w(m) end }

local REAL = emu
local frame_n = 0
local function frame(n)
  for _ = 1, n do pcall(function() REAL:runFrame() end); frame_n = frame_n + 1 end
end
local function pad(k, hold) REAL:setKeys(1 << k); frame(hold or 4); REAL:setKeys(0); frame(3) end
local A, B, START, DOWN = 0, 1, 3, 7

local shots = 0
local function shot()
  shots = shots + 1
  pcall(function() REAL:screenshot(string.format("%s%02d.png", SH, shots)) end)
end

w("=== boot ===")
frame(260); shot()

-- Phase 1: lots of A to clear the splash, title and Oak's opening speech.
w("=== phase 1: A-mash ===")
for i = 1, 150 do
  pad(A, 4)
  if i % 30 == 0 then shot() end
end

-- Phase 2: mixed confirm work for the naming screens (START opens the menu, A confirms;
-- B cancels anything accidentally opened).
w("=== phase 2: START/A/B mix ===")
for i = 1, 120 do
  pad(START, 4)
  pad(A, 4)
  if i % 3 == 0 then pad(A, 4) end
  if i % 10 == 0 then pad(B, 4) end
  if i % 20 == 0 then shot() end
end

-- Phase 3: long A run to finish the remaining intro dialogue and land in the overworld.
w("=== phase 3: long A run ===")
for i = 1, 300 do
  pad(A, 4)
  if i % 50 == 0 then shot() end
end

w("=== frames simulated: " .. frame_n .. "  at " .. tostring(REAL:currentFrame()) .. " ===")
shot()
if lf then lf:close() end
